#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

ResourceRegistry::ResourceRegistry(RenderContext* pRenderContext) :
    m_RenderContext(pRenderContext), m_DrawItemDataDescriptorLayout(VK_NULL_HANDLE), m_HostBufferPoolI(), m_HostBufferPoolV()
{
    // Reset pool sizes.
    m_HostBufferPoolSizeI.store(0LL);
    m_HostBufferPoolSizeV.store(0LL);

    m_CommitTaskBusy.store(false);

    // Create a descriptor set layout defining resource arrays for draw items.
    // --------------------------------------------

    std::vector<VkDescriptorBindingFlags> bindingFlags(3U, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT);

    VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorSetFlags { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
    {
        descriptorSetFlags.bindingCount  = static_cast<uint32_t>(bindingFlags.size());
        descriptorSetFlags.pBindingFlags = bindingFlags.data();
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    {
        // Binding 0: Index Buffers
        bindings.push_back(VkDescriptorSetLayoutBinding(0U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));

        // Binding 1: Vertex Buffers
        bindings.push_back(VkDescriptorSetLayoutBinding(1U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));

        // Binding 2: Meta-data
        bindings.push_back(VkDescriptorSetLayoutBinding(2U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetLayoutInfo.pBindings    = bindings.data();
        descriptorSetLayoutInfo.pNext        = &descriptorSetFlags;
    }
    Check(vkCreateDescriptorSetLayout(pRenderContext->GetDevice(), &descriptorSetLayoutInfo, nullptr, &m_DrawItemDataDescriptorLayout),
          "Failed to create descriptor set layout for resource registry.");
}

void ResourceRegistry::BuildDescriptors()
{
    VkDescriptorSetAllocateInfo descriptorsAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    {
        descriptorsAllocInfo.descriptorSetCount = 1U;
        descriptorsAllocInfo.descriptorPool     = m_RenderContext->GetDescriptorPool();
        descriptorsAllocInfo.pSetLayouts        = &m_DrawItemDataDescriptorLayout;
    }

    Check(vkAllocateDescriptorSets(m_RenderContext->GetDevice(), &descriptorsAllocInfo, &m_DrawItemDataDescriptorSet),
          "Failed to allocate indexed resource descriptor sets.");

    auto WriteDrawItemBufferDescriptor = [&](uint32_t dstBinding, uint32_t dstIndex, VkBuffer buffer)
    {
        VkDescriptorBufferInfo bufferInfo {};
        {
            bufferInfo.buffer = buffer;
            bufferInfo.range  = VK_WHOLE_SIZE;
        }

        VkWriteDescriptorSet descriptorWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        {
            descriptorWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrite.descriptorCount = 1U;
            descriptorWrite.dstSet          = m_DrawItemDataDescriptorSet;
            descriptorWrite.dstBinding      = dstBinding;
            descriptorWrite.dstArrayElement = dstIndex;
            descriptorWrite.pBufferInfo     = &bufferInfo;
        }
        vkUpdateDescriptorSets(m_RenderContext->GetDevice(), 1U, &descriptorWrite, 0U, nullptr);
    };

    for (uint32_t drawItemIndex = 0U; drawItemIndex < m_DrawItems.size(); drawItemIndex++)
    {
        auto& drawItem = m_DrawItems[drawItemIndex];

        WriteDrawItemBufferDescriptor(0U, drawItemIndex, drawItem.bufferI.buffer);
        WriteDrawItemBufferDescriptor(1U, drawItemIndex, drawItem.bufferV.buffer);
    }

    // Also write the meta-data.
    WriteDrawItemBufferDescriptor(2U, 0U, m_DrawItemMetaDataBuffer.buffer);

    spdlog::info("Created draw item buffer descriptors.");
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

            auto requestCount = static_cast<uint32_t>(m_DrawItemRequests.size());
            auto requestIndex = 0U;

            // Track meta-data.
            std::vector<DrawItemMetaData> drawItemMetaData;

            // Clear the requests.
            while (!m_DrawItemRequests.empty())
            {
                auto request = m_DrawItemRequests.front();

                spdlog::info("Upload GPU Mesh ----> [{} / {}]", requestIndex++, requestCount);

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

                DebugLabelBufferResource(m_RenderContext, drawItem.bufferI, "IndexBuffer");
                DebugLabelBufferResource(m_RenderContext, drawItem.bufferV, "VertexBuffer");

                // Push the gpu meta-data about the draw item.
                DrawItemMetaData metaData {};
                {
                    metaData.matrix    = drawItem.pMesh->GetLocalToWorld();
                    metaData.faceCount = drawItem.indexCount / 3U;
                }
                drawItemMetaData.push_back(metaData);

                // Request processed, remove.
                m_DrawItemRequests.pop();
            }

            // Upload the meta-data.
            {
                createInfo.pData         = drawItemMetaData.data();
                createInfo.size          = sizeof(DrawItemMetaData) * drawItemMetaData.size();
                createInfo.pBufferDevice = &m_DrawItemMetaDataBuffer;
                createInfo.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                m_RenderContext->CreateDeviceBufferWithData(createInfo);
            }

            DebugLabelBufferResource(m_RenderContext, m_DrawItemMetaDataBuffer, "DrawItemMetaDataBuffer");

            // Free the scratch memory.
            vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

            // Create descriptors for the uploaded buffers.
            BuildDescriptors();

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

    vkDestroyDescriptorSetLayout(m_RenderContext->GetDevice(), m_DrawItemDataDescriptorLayout, nullptr);

    vmaDestroyBuffer(m_RenderContext->GetAllocator(), m_DrawItemMetaDataBuffer.buffer, m_DrawItemMetaDataBuffer.bufferAllocation);

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
