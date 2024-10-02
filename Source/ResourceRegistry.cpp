#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

#include <cstddef>

void CreateDrawItemDescriptorLayout(RenderContext* pRenderContext, VkDescriptorSetLayout& descriptorLayout)
{
    std::vector<VkDescriptorBindingFlags> bindingFlags(2U, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT);

    // The last binding is fully bound / normal.
    bindingFlags.push_back(0x0);

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
    Check(vkCreateDescriptorSetLayout(pRenderContext->GetDevice(), &descriptorSetLayoutInfo, nullptr, &descriptorLayout),
          "Failed to create descriptor set layout for resource registry.");
}

void CreateMaterialDataDescriptorLayout(RenderContext* pRenderContext, VkDescriptorSetLayout& descriptorLayout)
{
    std::vector<VkDescriptorBindingFlags> bindingFlags(1U, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT);

    VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorSetFlags { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
    {
        descriptorSetFlags.bindingCount  = static_cast<uint32_t>(bindingFlags.size());
        descriptorSetFlags.pBindingFlags = bindingFlags.data();
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    {
        // Binding 0: Albedo Images
        bindings.push_back(VkDescriptorSetLayoutBinding(0U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        descriptorSetLayoutInfo.pBindings    = bindings.data();
        descriptorSetLayoutInfo.pNext        = &descriptorSetFlags;
    }
    Check(vkCreateDescriptorSetLayout(pRenderContext->GetDevice(), &descriptorSetLayoutInfo, nullptr, &descriptorLayout),
          "Failed to create descriptor set layout for resource registry.");
}

ResourceRegistry::ResourceRegistry(RenderContext* pRenderContext) :
    m_RenderContext(pRenderContext), m_DrawItemDataDescriptorLayout(VK_NULL_HANDLE), m_DrawItemDataDescriptorSet(VK_NULL_HANDLE),
    m_MaterialDataDescriptorLayout(VK_NULL_HANDLE), m_MaterialDataDescriptorSet(VK_NULL_HANDLE), m_HostBufferPoolSize(0LL), m_HostImagePoolSize(0LL)
{
    // Create descriptor set layouts.
    CreateDrawItemDescriptorLayout(m_RenderContext, m_DrawItemDataDescriptorLayout);
    CreateMaterialDataDescriptorLayout(m_RenderContext, m_MaterialDataDescriptorLayout);

    // Reserve pool memory.
    m_HostBufferPool.resize(kHostBufferPoolMaxBytes);
    m_HostImagePool.resize(kHostImagePoolMaxBytes);

    m_CommitTaskBusy.store(false);
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
            RenderContext::CreateDeviceBufferWithDataParams deviceBufferCreateParams {};
            {
                deviceBufferCreateParams.pBufferStaging = &stagingBuffer;

                // Obtain a thread-local command pool for submission work on this thread.
                m_RenderContext->CreateCommandPool(&deviceBufferCreateParams.commandPool);
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

                spdlog::info("Upload GPU Mesh ----> [{} / {}]", ++requestIndex, requestCount);

                DrawItem drawItem;

                // Forward the mesh pointer.
                drawItem.pMesh = request.pMesh;

                // Compute index count.
                drawItem.indexCount = static_cast<uint32_t>(request.indexBufferSize) / sizeof(uint32_t);

                // Create index buffer.
                {
                    deviceBufferCreateParams.pData         = request.pIndexBufferHost;
                    deviceBufferCreateParams.size          = request.indexBufferSize;
                    deviceBufferCreateParams.pBufferDevice = &drawItem.bufferI;
                    deviceBufferCreateParams.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                    m_RenderContext->CreateDeviceBufferWithData(deviceBufferCreateParams);
                }

                // Create vertex buffer.
                {
                    deviceBufferCreateParams.pData         = request.pVertexBufferHost;
                    deviceBufferCreateParams.size          = request.vertexBufferSize;
                    deviceBufferCreateParams.pBufferDevice = &drawItem.bufferV;
                    deviceBufferCreateParams.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                    m_RenderContext->CreateDeviceBufferWithData(deviceBufferCreateParams);
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
                deviceBufferCreateParams.pData         = drawItemMetaData.data();
                deviceBufferCreateParams.size          = sizeof(DrawItemMetaData) * drawItemMetaData.size();
                deviceBufferCreateParams.pBufferDevice = &m_DrawItemMetaDataBuffer;
                deviceBufferCreateParams.usage         = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                m_RenderContext->CreateDeviceBufferWithData(deviceBufferCreateParams);
            }

            requestCount = static_cast<uint32_t>(m_MaterialRequests.size());
            requestIndex = 0U;

            // Initialize the device image upload info.
            RenderContext::CreateDeviceImageWithDataParams deviceImageCreateParams {};
            {
                deviceImageCreateParams.pBufferStaging = &stagingBuffer;

                // Obtain a thread-local command pool for submission work on this thread.
                // (Re-use the one made for buffer creation).
                deviceImageCreateParams.commandPool = deviceBufferCreateParams.commandPool;
            }

            // Create a base image description.
            deviceImageCreateParams.info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            {
                deviceImageCreateParams.info.imageType   = VK_IMAGE_TYPE_2D;
                deviceImageCreateParams.info.arrayLayers = 1U;
                deviceImageCreateParams.info.mipLevels   = 1U;
                deviceImageCreateParams.info.samples     = VK_SAMPLE_COUNT_1_BIT;
                deviceImageCreateParams.info.tiling      = VK_IMAGE_TILING_OPTIMAL;
                deviceImageCreateParams.info.flags       = 0x0;
                deviceImageCreateParams.info.usage       = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }

            DebugLabelBufferResource(m_RenderContext, m_DrawItemMetaDataBuffer, "DrawItemMetaDataBuffer");

            // Reset the device material list.
            m_DeviceMaterials.clear();

            // Process mesh requests.
            while (!m_MaterialRequests.empty())
            {
                auto request = m_MaterialRequests.front();

                spdlog::info("Upload GPU Material ----> [{} / {}]", ++requestIndex, requestCount);

                DeviceMaterial deviceMaterial;

                // Albedo
                {
                    deviceImageCreateParams.pData         = request.albedo.data;
                    deviceImageCreateParams.pImageDevice  = &deviceMaterial.albedo;
                    deviceImageCreateParams.bytesPerTexel = static_cast<VkDeviceSize>(request.albedo.stride);
                    deviceImageCreateParams.info.format   = request.albedo.format;
                    deviceImageCreateParams.info.extent   = { static_cast<uint32_t>(request.albedo.dim[0]),
                                                              static_cast<uint32_t>(request.albedo.dim[1]),
                                                              1U };

                    m_RenderContext->CreateDeviceImageWithData(deviceImageCreateParams);
                }

                m_DeviceMaterials.push_back(deviceMaterial);

                // Request processed, remove.
                m_MaterialRequests.pop();
            }

            // Free the scratch memory.
            vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

            // Create descriptors for the uploaded buffers.
            BuildDescriptors();

            // Free the thread local command pool.
            vkDestroyCommandPool(m_RenderContext->GetDevice(), deviceBufferCreateParams.commandPool, nullptr);

            // Free host memory for buffer and image pools.
            {
                m_HostBufferPool.clear();
                m_HostBufferPool.shrink_to_fit();

                m_HostImagePool.clear();
                m_HostImagePool.shrink_to_fit();

                m_HostBufferPoolSize = 0LL;
                m_HostImagePoolSize  = 0LL;
            }

            spdlog::info("Graphics resource upload complete.");

            // Idle.
            m_CommitTaskBusy.store(false);
        });
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    vkDestroyDescriptorSetLayout(m_RenderContext->GetDevice(), m_DrawItemDataDescriptorLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_RenderContext->GetDevice(), m_MaterialDataDescriptorLayout, nullptr);

    vmaDestroyBuffer(m_RenderContext->GetAllocator(), m_DrawItemMetaDataBuffer.buffer, m_DrawItemMetaDataBuffer.bufferAllocation);

    for (auto& drawItem : m_DrawItems)
    {
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), drawItem.bufferI.buffer, drawItem.bufferI.bufferAllocation);
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), drawItem.bufferV.buffer, drawItem.bufferV.bufferAllocation);
    }

    auto ReleaseDeviceMaterialImage = [this](Image* pImage)
    {
        if (pImage->imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_RenderContext->GetDevice(), pImage->imageView, nullptr);

        vmaDestroyImage(m_RenderContext->GetAllocator(), pImage->image, pImage->imageAllocation);
    };

    for (auto& deviceMaterial : m_DeviceMaterials)
    {
        ReleaseDeviceMaterialImage(&deviceMaterial.albedo);
    }
}

// ------------------------------------------------

void ResourceRegistry::PushDrawItemRequest(DrawItemRequest& request)
{
    std::lock_guard<std::mutex> lock(m_HostBufferPoolMutex);

    auto bufferSizeIPrev = m_HostBufferPoolSize;
    m_HostBufferPoolSize += request.indexBufferSize;

    auto bufferSizeVPrev = m_HostBufferPoolSize;
    m_HostBufferPoolSize += request.vertexBufferSize;

    // Map a pointer back in the pool that the client can fill with data.
    request.pIndexBufferHost  = reinterpret_cast<void*>(&m_HostBufferPool.at(bufferSizeIPrev));
    request.pVertexBufferHost = reinterpret_cast<void*>(&m_HostBufferPool.at(bufferSizeVPrev));

    // Push the request.
    m_DrawItemRequests.push(request);
}

void ResourceRegistry::PushMaterialRequest(MaterialRequest& request)
{
    std::lock_guard<std::mutex> lock(m_HostImagePoolMutex);

    auto imageSizeAlbedoPrev = m_HostImagePoolSize;
    m_HostImagePoolSize += static_cast<uint64_t>(request.albedo.stride * request.albedo.dim[0]) * request.albedo.dim[1];

    // Map a pointer back in the pool that the client can fill with data.
    request.albedo.data = reinterpret_cast<void*>(&m_HostImagePool.at(imageSizeAlbedoPrev));

    m_MaterialRequests.push(request);
}
