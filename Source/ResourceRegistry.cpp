#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

void InterleaveImageAlpha(stbi_uc** pImageData, int& width, int& height, int& channels)
{
    auto* pAlphaImage = new unsigned char[width * height * 4U]; // NOLINT

    for (int i = 0; i < width * height; ++i)
    {
        pAlphaImage[i * 4 + 0] = (*pImageData)[i * 3 + 0]; // NOLINT R
        pAlphaImage[i * 4 + 1] = (*pImageData)[i * 3 + 1]; // NOLINT G
        pAlphaImage[i * 4 + 2] = (*pImageData)[i * 3 + 2]; // NOLINT B
        pAlphaImage[i * 4 + 3] = 255;                      // NOLINT A (fully opaque)
    }

    // Free the original image memory
    stbi_image_free(*pImageData);

    *pImageData = pAlphaImage;

    // There is now an alpha channel.
    channels = 4U;
}

void ImageBarrier(RenderContext*        pRenderContext,
                  VkCommandBuffer       vkCommand,
                  VkImage               vkImage,
                  VkImageLayout         vkLayoutOld,
                  VkImageLayout         vkLayoutNew,
                  VkAccessFlags2        vkAccessSrc,
                  VkAccessFlags2        vkAccessDst,
                  VkPipelineStageFlags2 vkStageSrc,
                  VkPipelineStageFlags2 vkStageDst)
{
    VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    {
        vkImageBarrier.image               = vkImage;
        vkImageBarrier.oldLayout           = vkLayoutOld;
        vkImageBarrier.newLayout           = vkLayoutNew;
        vkImageBarrier.srcAccessMask       = vkAccessSrc;
        vkImageBarrier.dstAccessMask       = vkAccessDst;
        vkImageBarrier.srcStageMask        = vkStageSrc;
        vkImageBarrier.dstStageMask        = vkStageDst;
        vkImageBarrier.srcQueueFamilyIndex = pRenderContext->GetCommandQueueIndex();
        vkImageBarrier.dstQueueFamilyIndex = pRenderContext->GetCommandQueueIndex();
        vkImageBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }

    VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    {
        vkDependencyInfo.imageMemoryBarrierCount = 1U;
        vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
    }

    vkCmdPipelineBarrier2(vkCommand, &vkDependencyInfo);
};

void ProcessMaterialRequest(RenderContext* pRenderContext, const ResourceRegistry::MaterialRequest& materialRequest, Buffer& stagingBuffer, Image& albedoImage)
{
    spdlog::info("Processing Material Request for {}", materialRequest.id.GetName());

    if (materialRequest.imagePathAlbedo.GetResolvedPath().empty())
        return;

    int   width      = 0;
    int   height     = 0;
    int   channels   = 0;
    auto* pImageData = stbi_load(materialRequest.imagePathAlbedo.GetResolvedPath().c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (channels == 3U)
        InterleaveImageAlpha(&pImageData, width, height, channels);

    // Copy Host -> Staging Memory.
    // -----------------------------------------------------

    void* pMappedStagingMemory = nullptr;
    Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation, &pMappedStagingMemory), "Failed to map a pointer to staging memory.");
    {
        // Copy from Host -> Staging memory.
        memcpy(pMappedStagingMemory, pImageData, channels * width * height); // NOLINT

        vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation);
    }

    stbi_image_free(pImageData);

    // Create dedicate device memory for the image
    // -----------------------------------------------------

    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageInfo.arrayLayers       = 1U;
    imageInfo.mipLevels         = 1U;
    imageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling            = VK_IMAGE_TILING_LINEAR;
    imageInfo.extent            = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1U };
    imageInfo.flags             = 0x0;
    imageInfo.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.format            = VK_FORMAT_R8G8B8A8_SRGB;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Check(vmaCreateImage(pRenderContext->GetAllocator(), &imageInfo, &allocInfo, &albedoImage.image, &albedoImage.imageAllocation, nullptr), "Failed to create dedicated image memory.");

    // Create Image View.
    // -----------------------------------------------------
    VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    {
        imageViewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.image            = albedoImage.image;
        imageViewInfo.format           = VK_FORMAT_R8G8B8A8_SRGB;
        imageViewInfo.components       = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }
    Check(vkCreateImageView(pRenderContext->GetDevice(), &imageViewInfo, nullptr, &albedoImage.imageView), "Failed to create material image view.");

    // Copy Staging -> Device Memory.
    // -----------------------------------------------------

    VkCommandBufferAllocateInfo vkCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    {
        vkCommandAllocateInfo.commandBufferCount = 1U;
        vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
    }

    VkCommandBuffer vkCommand = VK_NULL_HANDLE;
    Check(vkAllocateCommandBuffers(pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommand),
          "Failed to created command buffer for uploading scene resource "
          "memory.");

    VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    {
        vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo), "Failed to begin recording upload commands");

    VmaAllocationInfo allocationInfo;
    vmaGetAllocationInfo(pRenderContext->GetAllocator(), albedoImage.imageAllocation, &allocationInfo);

    ImageBarrier(pRenderContext,
                 vkCommand,
                 albedoImage.image,
                 VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_ACCESS_2_NONE,
                 VK_ACCESS_2_MEMORY_READ_BIT,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy bufferImageCopyInfo;
    {
        bufferImageCopyInfo.bufferOffset      = 0U;
        bufferImageCopyInfo.bufferImageHeight = 0U;
        bufferImageCopyInfo.bufferRowLength   = 0U;
        bufferImageCopyInfo.imageExtent       = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1U };
        bufferImageCopyInfo.imageOffset       = { 0U, 0U, 0U };
        bufferImageCopyInfo.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
    }
    vkCmdCopyBufferToImage(vkCommand, stagingBuffer.buffer, albedoImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &bufferImageCopyInfo);

    ImageBarrier(pRenderContext,
                 vkCommand,
                 albedoImage.image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_2_NONE,
                 VK_ACCESS_2_MEMORY_READ_BIT,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR);

    Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

    VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    {
        vkSubmitInfo.commandBufferCount = 1U;
        vkSubmitInfo.pCommandBuffers    = &vkCommand;
    }
    Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1U, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

    // Wait for the copy to complete.
    // -----------------------------------------------------

    Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for copy commands to finish dispatching.");
}

void ProcessMeshRequest(RenderContext*                       pRenderContext,
                        const ResourceRegistry::MeshRequest& meshRequest,
                        Buffer&                              stagingBuffer,
                        Buffer&                              positionBuffer,
                        Buffer&                              normalBuffer,
                        Buffer&                              indexBuffer,
                        Buffer&                              texCoordBuffer)
{
    spdlog::info("Processing Mesh Request for {}", meshRequest.id.GetName());

    auto CreateMeshBuffer = [&](const void* pData, uint32_t dataSize, VkBufferUsageFlags usage)
    {
        // Create dedicate device memory for the mesh buffer.
        // -----------------------------------------------------

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = dataSize;
        bufferInfo.usage              = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        Buffer meshBuffer {};
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &meshBuffer.buffer, &meshBuffer.bufferAllocation, nullptr), "Failed to create dedicated buffer memory.");

#ifdef _DEBUG
        // Label the allocation.
        vmaSetAllocationName(pRenderContext->GetAllocator(), meshBuffer.bufferAllocation, std::format("Index Buffer Alloc - [{}]", meshRequest.id.GetName()).c_str());

        // Label the buffer object.
        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(meshBuffer.buffer), std::format("Vertex Buffer - [{}]", meshRequest.id.GetName()));
#endif
        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedData = nullptr;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation, &pMappedData), "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, dataSize);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation);
        }

        // Copy Staging -> Device Memory.
        // -----------------------------------------------------

        VkCommandBufferAllocateInfo vkCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        {
            vkCommandAllocateInfo.commandBufferCount = 1U;
            vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
        }

        VkCommandBuffer vkCommand = VK_NULL_HANDLE;
        Check(vkAllocateCommandBuffers(pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommand),
              "Failed to created command buffer for uploading scene resource "
              "memory.");

        VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        {
            vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo), "Failed to begin recording upload commands");

        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), meshBuffer.bufferAllocation, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0U;
            copyInfo.dstOffset = 0U;
            copyInfo.size      = dataSize;
        }
        vkCmdCopyBuffer(vkCommand, stagingBuffer.buffer, meshBuffer.buffer, 1U, &copyInfo);

        Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

        VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            vkSubmitInfo.commandBufferCount = 1U;
            vkSubmitInfo.pCommandBuffers    = &vkCommand;
        }
        Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1U, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

        // Wait for the copy to complete.
        // -----------------------------------------------------

        Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for copy commands to finish dispatching.");

        return meshBuffer;
    };

    indexBuffer    = CreateMeshBuffer(meshRequest.pIndices.data(), sizeof(GfVec3i) * static_cast<uint32_t>(meshRequest.pIndices.size()), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    positionBuffer = CreateMeshBuffer(meshRequest.pPoints.data(), sizeof(GfVec3f) * static_cast<uint32_t>(meshRequest.pPoints.size()), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    normalBuffer   = CreateMeshBuffer(meshRequest.pNormals.data(), sizeof(GfVec3f) * static_cast<uint32_t>(meshRequest.pNormals.size()), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    texCoordBuffer = CreateMeshBuffer(meshRequest.pTexCoords.data(), sizeof(GfVec2f) * static_cast<uint32_t>(meshRequest.pTexCoords.size()), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void ResourceRegistry::SyncDescriptorSets(RenderContext* pRenderContext, const std::array<Image, kMaxImageResources>& imageResources, std::vector<VkDescriptorSet>& descriptorSets)
{
    // First nuke the descriptor pool.
    vkResetDescriptorPool(pRenderContext->GetDevice(), pRenderContext->GetDescriptorPool(), 0x0);

    // Clear the sets.
    descriptorSets.resize(imageResources.size());

    // Due to Vulkan API design need to fill a list of the same set layout for each set.
    std::vector<VkDescriptorSetLayout> descriptorSetLayout(imageResources.size(), m_DescriptorSetLayout);

    VkDescriptorSetAllocateInfo descriptorSetAllocationInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    {
        descriptorSetAllocationInfo.descriptorPool     = pRenderContext->GetDescriptorPool();
        descriptorSetAllocationInfo.descriptorSetCount = static_cast<uint32_t>(imageResources.size()); // WARNING: This is temporary while 1 image = 1 set.
        descriptorSetAllocationInfo.pSetLayouts        = descriptorSetLayout.data();
    }
    Check(vkAllocateDescriptorSets(pRenderContext->GetDevice(), &descriptorSetAllocationInfo, descriptorSets.data()), "Failed to allocate material descriptors.");

    // Update the descriptor sets.
    std::vector<VkWriteDescriptorSet> descriptorSetWrites;

    for (uint32_t imageIndex = 0U; imageIndex < imageResources.size(); imageIndex++)
    {
        VkDescriptorImageInfo descriptorImageInfo;
        {
            descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descriptorImageInfo.imageView   = imageResources.at(imageIndex).imageView;
            descriptorImageInfo.sampler     = VK_NULL_HANDLE;
        }

        VkWriteDescriptorSet writeInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        {
            writeInfo.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writeInfo.descriptorCount = 1U;
            writeInfo.dstBinding      = 0U;
            writeInfo.dstSet          = descriptorSets.at(imageIndex);
            writeInfo.pImageInfo      = &descriptorImageInfo;
        }
        descriptorSetWrites.push_back(writeInfo);
    }

    vkUpdateDescriptorSets(pRenderContext->GetDevice(), static_cast<uint32_t>(descriptorSetWrites.size()), descriptorSetWrites.data(), 0U, nullptr);
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty())
        return;

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

        Check(vmaCreateBuffer(m_RenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.bufferAllocation, nullptr), "Failed to create staging buffer memory.");
    }

    // Commit Mesh Requests
    // ----------------------------------------------------------

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        const uint64_t& i = 4U * meshRequest.first;
        ProcessMeshRequest(m_RenderContext, meshRequest.second, stagingBuffer, m_BufferResources.at(i + 0U), m_BufferResources.at(i + 1U), m_BufferResources.at(i + 2U), m_BufferResources.at(i + 3U));

        // Request process, remove.
        m_PendingMeshRequests.pop();
    }

    // Commit Material Requests
    // -----------------------------------------------------------

    while (!m_PendingMaterialRequests.empty())
    {
        auto materialRequest = m_PendingMaterialRequests.front();

        const uint64_t& i = 1U * materialRequest.first;
        ProcessMaterialRequest(m_RenderContext, materialRequest.second, stagingBuffer, m_ImageResources.at(i + 0U));

        m_PendingMaterialRequests.pop();
    }

    // Release staging memory.
    vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

    // Configure Descriptor Set Layouts
    // --------------------------------------

    Check(CreatePhysicallyBasedMaterialDescriptorLayout(m_RenderContext->GetDevice(), m_DescriptorSetLayout),
          "Failed to create a Vulkan Descriptor Set Layout for Physically Based "
          "Materials.");

    SyncDescriptorSets(m_RenderContext, m_ImageResources, m_DescriptorSets);
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (uint32_t bufferIndex = 0U; bufferIndex < 4U * m_MeshCounter; bufferIndex++)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), m_BufferResources.at(bufferIndex).buffer, m_BufferResources.at(bufferIndex).bufferAllocation);

    for (uint32_t imageIndex = 0U; imageIndex < 1U * m_MaterialCounter; imageIndex++)
    {
        if (m_ImageResources.at(imageIndex).imageView != VK_NULL_HANDLE)
            vkDestroyImageView(m_RenderContext->GetDevice(), m_ImageResources.at(imageIndex).imageView, nullptr);

        vmaDestroyImage(m_RenderContext->GetAllocator(), m_ImageResources.at(imageIndex).image, m_ImageResources.at(imageIndex).imageAllocation);
    }

    vkDestroyDescriptorSetLayout(m_RenderContext->GetDevice(), m_DescriptorSetLayout, nullptr);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, Buffer& positionBuffer, Buffer& normalBuffer, Buffer& indexBuffer, Buffer& texCoordBuffer)
{
    positionBuffer = m_BufferResources.at(4U * resourceHandle + 0U);
    normalBuffer   = m_BufferResources.at(4U * resourceHandle + 1U);
    indexBuffer    = m_BufferResources.at(4U * resourceHandle + 2U);
    texCoordBuffer = m_BufferResources.at(4U * resourceHandle + 3U);

    return true;
}

bool ResourceRegistry::GetMaterialResources(uint64_t resourceHandle, Image& albedoImage) { return false; }
