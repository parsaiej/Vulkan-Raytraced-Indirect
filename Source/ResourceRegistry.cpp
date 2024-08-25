#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

void CreateSampledImageResource(RenderContext* pRenderContext, Image* pImage, uint32_t imageWidth, uint32_t imageHeight, VkFormat imageFormat)
{
    // Create dedicate device memory for the image
    // -----------------------------------------------------

    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageInfo.arrayLayers       = 1U;
    imageInfo.mipLevels         = 1U;
    imageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.extent            = { static_cast<uint32_t>(imageWidth), static_cast<uint32_t>(imageHeight), 1U };
    imageInfo.flags             = 0x0;
    imageInfo.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.format            = imageFormat;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Check(vmaCreateImage(pRenderContext->GetAllocator(), &imageInfo, &allocInfo, &pImage->image, &pImage->imageAllocation, nullptr),
          "Failed to create dedicated image memory.");

    // Create Image View.
    // -----------------------------------------------------

    VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    {
        imageViewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.image            = pImage->image;
        imageViewInfo.format           = imageInfo.format;
        imageViewInfo.components       = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }
    Check(vkCreateImageView(pRenderContext->GetDevice(), &imageViewInfo, nullptr, &pImage->imageView), "Failed to create sampled image view.");
}

ResourceRegistry::ResourceRegistry(RenderContext* pRenderContext) : m_RenderContext(pRenderContext)
{
    // Create default image.
    auto* pDefaultImage = &m_ImageResources.at(m_ImageCounter++);
    CreateSampledImageResource(pRenderContext, pDefaultImage, 4U, 4U, VK_FORMAT_R8G8B8A8_SRGB);
    DebugLabelImageResource(pRenderContext, *pDefaultImage, "Default Material Image");

    // Create default buffer.
    auto* pDefaultBuffer = &m_BufferResources.at(m_BufferCounter++);

    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = 16U;
        bufferInfo.usage              = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        Check(m_BufferCounter + 1U < kMaxBufferResources, "Exceeded the maximum buffer resources.");

        Check(vmaCreateBuffer(pRenderContext->GetAllocator(),
                              &bufferInfo,
                              &allocInfo,
                              &pDefaultBuffer->buffer,
                              &pDefaultBuffer->bufferAllocation,
                              nullptr),
              "Failed to create dedicated buffer memory.");
    }

    DebugLabelBufferResource(pRenderContext, *pDefaultBuffer, "Default Mesh Buffer");

    // Create a command pool for the resource registry (in case of async loading).

    VkCommandPoolCreateInfo vkCommandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    {
        vkCommandPoolInfo.queueFamilyIndex = pRenderContext->GetCommandQueueIndex();
        vkCommandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    Check(vkCreateCommandPool(pRenderContext->GetDevice(), &vkCommandPoolInfo, nullptr, &m_ResourceCreationCommandPool),
          "Failed to create a Resource Creation Command Pool");
}

// Local utility for emplacing an alpha value every 12 bytes.
void InterleaveImageAlpha(stbi_uc** pImageData, int& width, int& height, int& channels)
{
    auto* pAlphaImage = new unsigned char[width * height * 4U]; // NOLINT

    for (int i = 0; i < width * height; ++i)
    {
        switch (channels)
        {
            case 1U:
                pAlphaImage[i * 4 + 0] = (*pImageData)[i * channels + 0]; // NOLINT R
                pAlphaImage[i * 4 + 1] = (*pImageData)[i * channels + 0]; // NOLINT G
                pAlphaImage[i * 4 + 2] = (*pImageData)[i * channels + 0]; // NOLINT B
                break;
            case 3U:
                pAlphaImage[i * 4 + 0] = (*pImageData)[i * channels + 0]; // NOLINT R
                pAlphaImage[i * 4 + 1] = (*pImageData)[i * channels + 1]; // NOLINT G
                pAlphaImage[i * 4 + 2] = (*pImageData)[i * channels + 2]; // NOLINT B
                break;
        }

        pAlphaImage[i * 4 + 3] = 255; // NOLINT A (fully opaque)
    }

    // Free the original image memory
    stbi_image_free(*pImageData);

    *pImageData = pAlphaImage;

    // There is now an alpha channel.
    channels = 4U;
}

void ResourceRegistry::ProcessMaterialRequest(RenderContext*                           pRenderContext,
                                              const ResourceRegistry::MaterialRequest& materialRequest,
                                              Buffer&                                  stagingBuffer,
                                              ResourceRegistry::MaterialResources*     pMaterial)
{
    spdlog::info("Processing Material Request for {}", materialRequest.id.GetName());

    auto TryCreateImage = [&](const SdfAssetPath& imageResourcePath, const char* debugName) -> Image
    {
        if (imageResourcePath.GetResolvedPath().empty())
            return m_ImageResources[0];

        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedStagingMemory = nullptr;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation, &pMappedStagingMemory),
              "Failed to map a pointer to staging memory.");

        int imageWidth  = 0U;
        int imageHeight = 0U;

        VkFormat imageFormat = VK_FORMAT_UNDEFINED;
        if (std::filesystem::path(imageResourcePath.GetResolvedPath()).extension().string() == ".dds")
        {
            dds::Image imageFile;
            Check(dds::readFile(imageResourcePath.GetResolvedPath(), &imageFile) == 0U, "Failed to load DDS image to memory.");

            // Read out the image data.
            imageWidth  = static_cast<int>(imageFile.width);
            imageHeight = static_cast<int>(imageFile.height);

            uint32_t pixelStrideBytes = dds::getBitsPerPixel(imageFile.format) >> 3U;

            // Copy the dds image to staging memory. (Zero-mip for now).
            memcpy(pMappedStagingMemory, imageFile.mipmaps.front().data(), pixelStrideBytes * imageWidth * imageHeight); // NOLINT

            imageFormat = dds::getVulkanFormat(imageFile.format, imageFile.supportsAlpha);
        }
        else
        {
            int   channels   = 0;
            auto* pImageData = stbi_load(imageResourcePath.GetResolvedPath().c_str(), &imageWidth, &imageHeight, &channels, 0U);

            if (channels != 4U)
                InterleaveImageAlpha(&pImageData, imageWidth, imageHeight, channels);

            // Copy the stb-loaded image to staging memory.
            memcpy(pMappedStagingMemory, pImageData, channels * imageWidth * imageHeight); // NOLINT

            stbi_image_free(pImageData);

            // Hardcode for now...
            imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
        }

        vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation);

        // Create dedicate device memory for the image
        // -----------------------------------------------------

        Check(m_ImageCounter + 1U < kMaxImageResources, "Exceeded the maximum image resources.");

        // Obtain a image resource from the pool.
        auto* pMaterialImage = &m_ImageResources.at(m_ImageCounter++);
        CreateSampledImageResource(pRenderContext, pMaterialImage, imageWidth, imageHeight, imageFormat);

        DebugLabelImageResource(pRenderContext, *pMaterialImage, std::format("{} - {}", materialRequest.id.GetText(), debugName).c_str());

        // Copy Staging -> Device Memory.
        // -----------------------------------------------------

        VkCommandBuffer vkCommand = VK_NULL_HANDLE;
        SingleShotCommandBegin(pRenderContext, vkCommand, m_ResourceCreationCommandPool);
        {
            VmaAllocationInfo allocationInfo;
            vmaGetAllocationInfo(pRenderContext->GetAllocator(), pMaterialImage->imageAllocation, &allocationInfo);

            VulkanColorImageBarrier(vkCommand,
                                    pMaterialImage->image,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_ACCESS_2_NONE,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT);

            VkBufferImageCopy bufferImageCopyInfo;
            {
                bufferImageCopyInfo.bufferOffset      = 0U;
                bufferImageCopyInfo.bufferImageHeight = 0U;
                bufferImageCopyInfo.bufferRowLength   = 0U;
                bufferImageCopyInfo.imageExtent       = { static_cast<uint32_t>(imageWidth), static_cast<uint32_t>(imageHeight), 1U };
                bufferImageCopyInfo.imageOffset       = { 0U, 0U, 0U };
                bufferImageCopyInfo.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
            }

            vkCmdCopyBufferToImage(vkCommand,
                                   stagingBuffer.buffer,
                                   pMaterialImage->image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1U,
                                   &bufferImageCopyInfo);

            VulkanColorImageBarrier(vkCommand,
                                    pMaterialImage->image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    VK_ACCESS_2_SHADER_READ_BIT,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        }
        SingleShotCommandEnd(pRenderContext, vkCommand);

        return *pMaterialImage;
    };

    pMaterial->albedo    = TryCreateImage(materialRequest.imagePathAlbedo, "Albedo");
    pMaterial->normal    = TryCreateImage(materialRequest.imagePathNormal, "Normal");
    pMaterial->metallic  = TryCreateImage(materialRequest.imagePathMetallic, "Metallic");
    pMaterial->roughness = TryCreateImage(materialRequest.imagePathRoughness, "Roughness");
}

void ResourceRegistry::ProcessMeshRequest(RenderContext*                       pRenderContext,
                                          const ResourceRegistry::MeshRequest& meshRequest,
                                          Buffer&                              stagingBuffer,
                                          ResourceRegistry::MeshResources*     pMesh)
{
    spdlog::info("Processing Mesh Request for {}", meshRequest.id.GetName());

    auto CreateMeshBuffer = [&]<typename T>(T dataSource, VkBufferUsageFlags usage) -> Buffer
    {
        if (dataSource.empty())
            return m_BufferResources[0];

        auto data = dataSource.data();
        auto size = dataSource.size() * sizeof(dataSource[0]);

        // Create dedicate device memory for the mesh buffer.
        // -----------------------------------------------------

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        Check(m_BufferCounter + 1U < kMaxBufferResources, "Exceeded the maximum buffer resources.");

        // Obtain a buffer resource from the pool.
        auto* pMeshBuffer = &m_BufferResources.at(m_BufferCounter++);

        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &pMeshBuffer->buffer, &pMeshBuffer->bufferAllocation, nullptr),
              "Failed to create dedicated buffer memory.");

        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedData = nullptr;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation, &pMappedData),
              "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, data, size);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation);
        }

        // Copy Staging -> Device Memory.
        // -----------------------------------------------------

        VkCommandBuffer vkCommand = VK_NULL_HANDLE;
        SingleShotCommandBegin(pRenderContext, vkCommand, m_ResourceCreationCommandPool);
        {
            VmaAllocationInfo allocationInfo;
            vmaGetAllocationInfo(pRenderContext->GetAllocator(), pMeshBuffer->bufferAllocation, &allocationInfo);

            VkBufferCopy copyInfo;
            {
                copyInfo.srcOffset = 0U;
                copyInfo.dstOffset = 0U;
                copyInfo.size      = size;
            }
            vkCmdCopyBuffer(vkCommand, stagingBuffer.buffer, pMeshBuffer->buffer, 1U, &copyInfo);
        }
        SingleShotCommandEnd(pRenderContext, vkCommand);

        return *pMeshBuffer;
    };

    pMesh->indices   = CreateMeshBuffer(meshRequest.pIndices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    pMesh->positions = CreateMeshBuffer(meshRequest.pPoints, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    pMesh->normals   = CreateMeshBuffer(meshRequest.pNormals, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    pMesh->texCoords = CreateMeshBuffer(meshRequest.pTexCoords, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // Label the resources.
    DebugLabelBufferResource(pRenderContext, pMesh->indices, std::format("{} - Index", meshRequest.id.GetText()).c_str());
    DebugLabelBufferResource(pRenderContext, pMesh->positions, std::format("{} - Position", meshRequest.id.GetText()).c_str());
    DebugLabelBufferResource(pRenderContext, pMesh->normals, std::format("{} - Normal", meshRequest.id.GetText()).c_str());
    DebugLabelBufferResource(pRenderContext, pMesh->texCoords, std::format("{} - Texcoord", meshRequest.id.GetText()).c_str());
}

void ResourceRegistry::CommitJob()
{
    m_CommitJobBusy.store(true);

    // Create a block of staging buffer memory.
    Buffer stagingBuffer {};
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 256mb.
        bufferInfo.size = static_cast<VkDeviceSize>(256U * 1024U * 1024U);

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        Check(vmaCreateBuffer(m_RenderContext->GetAllocator(),
                              &bufferInfo,
                              &allocInfo,
                              &stagingBuffer.buffer,
                              &stagingBuffer.bufferAllocation,
                              nullptr),
              "Failed to create staging buffer memory.");
    }

    // Commit Mesh Requests
    // ----------------------------------------------------------

    PROFILE_START("Process Mesh Requests");

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        // Convert the mesh request into mesh resources.
        ProcessMeshRequest(m_RenderContext, meshRequest, stagingBuffer, &m_MeshResourceMap[meshRequest.id.GetHash()]);

        // Request process, remove.
        m_PendingMeshRequests.pop();
    }

    PROFILE_END;

    // Commit Material Requests
    // -----------------------------------------------------------

    PROFILE_START("Process Material Requests");

    while (!m_PendingMaterialRequests.empty())
    {
        auto materialRequest = m_PendingMaterialRequests.front();

        // Convert the material request into material resources
        ProcessMaterialRequest(m_RenderContext, materialRequest, stagingBuffer, &m_MaterialResourceMap[materialRequest.id.GetHash()]);

        m_PendingMaterialRequests.pop();
    }

    PROFILE_END;

    // Release staging memory.
    vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

    m_CommitJobBusy.store(false);
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty() && m_PendingMaterialRequests.empty())
        return;

    if (IsBusy())
        return;

    m_CommitJobThread = std::jthread(&ResourceRegistry::CommitJob, this);
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (uint32_t bufferIndex = 0U; bufferIndex <= m_BufferCounter; bufferIndex++)
    {
        if (m_BufferResources.at(bufferIndex).bufferView != VK_NULL_HANDLE)
            vkDestroyBufferView(m_RenderContext->GetDevice(), m_BufferResources.at(bufferIndex).bufferView, nullptr);

        vmaDestroyBuffer(m_RenderContext->GetAllocator(),
                         m_BufferResources.at(bufferIndex).buffer,
                         m_BufferResources.at(bufferIndex).bufferAllocation);
    }

    for (uint32_t imageIndex = 0U; imageIndex < m_ImageCounter; imageIndex++)
    {
        if (m_ImageResources.at(imageIndex).imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_RenderContext->GetDevice(), m_ImageResources.at(imageIndex).imageView, nullptr);

        vmaDestroyImage(m_RenderContext->GetAllocator(), m_ImageResources.at(imageIndex).image, m_ImageResources.at(imageIndex).imageAllocation);
    }

    vkDestroyCommandPool(m_RenderContext->GetDevice(), m_ResourceCreationCommandPool, nullptr);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, MeshResources& meshResources)
{
    if (!m_MeshResourceMap.contains(resourceHandle))
        return false;

    meshResources = m_MeshResourceMap[resourceHandle];

    // Check the validity of index buffer.
    return meshResources.indices.buffer != m_BufferResources[0].buffer;
}

bool ResourceRegistry::GetMaterialResources(uint64_t resourceHandle, MaterialResources& materialResources)
{
    if (!m_MaterialResourceMap.contains(resourceHandle))
        return false;

    materialResources = m_MaterialResourceMap[resourceHandle];

    return true;
}
