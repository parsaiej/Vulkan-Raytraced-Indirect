#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

ResourceRegistry::ResourceRegistry(RenderContext* pRenderContext) : m_RenderContext(pRenderContext), m_HostBufferPoolI(), m_HostBufferPoolV()
{
    // Reset pool sizes.
    m_HostBufferPoolSizeI.store(0LL);
    m_HostBufferPoolSizeV.store(0LL);

    m_CommitTaskBusy.store(false);
}

//
// ---------------------------------------------------------

thread_local Buffer s_ThreadLocalScratchBuffer {}; // NOLINT

std::array<Buffer, 8U> s_PerThreadScratchBuffer;

#include <fmt/ostream.h>

void CreateThreadLocalScratchBuffer(RenderContext* pRenderContext)
{
    // spdlog::debug("Creating staging upload memory for thread [{}]", fmt::streamed(tbb::this_tbb_thread::get_id()));

    // WARNING: Each task thread gets 128mb of scratch upload memory.
    pRenderContext->CreateStagingBuffer(128LL * 1024 * 1024, &s_ThreadLocalScratchBuffer);
}

void ReleaseThreadLocalScratchBuffer(RenderContext* pRenderContext)
{
    vmaDestroyBuffer(pRenderContext->GetAllocator(), s_ThreadLocalScratchBuffer.buffer, s_ThreadLocalScratchBuffer.bufferAllocation);
}

//
// ---------------------------------------------------------

void ProcessDrawItemRequest(RenderContext* pRenderContext, Buffer& stagingBuffer, const DrawItemRequest& request)
{
    // Initialize the device buffer upload info.
    RenderContext::CreateDeviceBufferWithDataParams createInfo {};
    {
        createInfo.pBufferStaging = &stagingBuffer;

        // Obtain a thread-local command pool for submission work on this thread.
        // createInfo.commandPool = pRenderContext->GetThreadLocalCommandPool();
    }

    // Create vertex buffer.
    Buffer deviceBufferV;
    {
        createInfo.pData         = request.pVertexBufferHost;
        createInfo.size          = request.vertexBufferSize;
        createInfo.pBufferDevice = &deviceBufferV;
        createInfo.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        pRenderContext->CreateDeviceBufferWithData(createInfo);
    }

    // Create index buffer.
    Buffer deviceBufferI;
    {
        createInfo.pData         = request.pIndexBufferHost;
        createInfo.size          = request.indexBufferSize;
        createInfo.pBufferDevice = &deviceBufferI;
        createInfo.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        pRenderContext->CreateDeviceBufferWithData(createInfo);
    }
}

void ResourceRegistry::_Commit()
{
    if (m_CommitTaskBusy.load())
        return;

    if (m_DrawItemRequests.empty())
        return;

    m_CommitTask.run(
        [&]
        {
            // Busy.
            m_CommitTaskBusy.store(true);

            // WARNING: Hard-coded scratch memory of 512mb.
            Buffer stagingBuffer;
            m_RenderContext->CreateStagingBuffer(512LL * 1024 * 1024, &stagingBuffer);

            // Initialize the device buffer upload info.
            RenderContext::CreateDeviceBufferWithDataParams createInfo {};
            {
                createInfo.pBufferStaging = &stagingBuffer;

                // Obtain a thread-local command pool for submission work on this thread.
                m_RenderContext->CreateCommandPool(&createInfo.commandPool);
            }

            // Reset the draw items list (Warning: will leak VRAM currently).
            m_DrawItems.clear();

            // Clear the requests.
            while (!m_DrawItemRequests.empty())
            {
                auto request = m_DrawItemRequests.front();

                spdlog::info("Processing draw item request...");

                DrawItem drawItem;

                // Forward the mesh pointer.
                drawItem.pMesh = request.pMesh;

                // Compute index count.
                drawItem.indexCount = static_cast<uint32_t>(request.indexBufferSize) / sizeof(uint32_t);

                // Create index buffer.
                {
                    createInfo.pData         = request.pIndexBufferHost;
                    createInfo.size          = request.indexBufferSize;
                    createInfo.pBufferDevice = &drawItem.bufferI;
                    createInfo.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                    m_RenderContext->CreateDeviceBufferWithData(createInfo);
                }

                // Create vertex buffer.
                {
                    createInfo.pData         = request.pVertexBufferHost;
                    createInfo.size          = request.vertexBufferSize;
                    createInfo.pBufferDevice = &drawItem.bufferV;
                    createInfo.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                    m_RenderContext->CreateDeviceBufferWithData(createInfo);
                }

                // Push the draw item.
                m_DrawItems.push_back(drawItem);

                // Request processed, remove.
                m_DrawItemRequests.pop();
            }

            // Free the scratch memory.
            vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

            // Free the thread local command pool.
            vkDestroyCommandPool(m_RenderContext->GetDevice(), createInfo.commandPool, nullptr);

            spdlog::info("Graphics resource upload complete.");

            // Idle.
            m_CommitTaskBusy.store(false);
        });
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (auto& drawItem : m_DrawItems)
    {
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), drawItem.bufferI.buffer, drawItem.bufferI.bufferAllocation);
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), drawItem.bufferV.buffer, drawItem.bufferV.bufferAllocation);
    }
}

// ------------------------------------------------

void ResourceRegistry::AddMesh(Mesh* pMesh, uint64_t bufferSizeI, uint64_t bufferSizeV, void** ppBufferI, void** ppBufferV)
{
    std::lock_guard<std::mutex> lock(m_MeshAllocationMutex);

    auto bufferSizeIPrev = m_HostBufferPoolSizeI.fetch_add(bufferSizeI);
    auto bufferSizeVPrev = m_HostBufferPoolSizeV.fetch_add(bufferSizeV);

    *ppBufferI = reinterpret_cast<void*>(&m_HostBufferPoolI.at(bufferSizeIPrev));
    *ppBufferV = reinterpret_cast<void*>(&m_HostBufferPoolV.at(bufferSizeVPrev));

    // Push the request.
    m_DrawItemRequests.push({ pMesh, *ppBufferI, bufferSizeI, *ppBufferV, bufferSizeV });
}
