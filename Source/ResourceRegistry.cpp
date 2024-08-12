#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

void InterleaveImageAlpha(stbi_uc** pImageData, int& width, int& height, int& channels)
{
    auto* pAlphaImage = new unsigned char[width * height * 4]; // NOLINT

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
    VkImageSubresourceRange imageSubresource;
    {
        imageSubresource.levelCount     = 1U;
        imageSubresource.layerCount     = 1U;
        imageSubresource.baseMipLevel   = 0U;
        imageSubresource.baseArrayLayer = 0U;
        imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    }

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
        vkImageBarrier.subresourceRange    = imageSubresource;
    }

    VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    {
        vkDependencyInfo.imageMemoryBarrierCount = 1U;
        vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
    }

    vkCmdPipelineBarrier2(vkCommand, &vkDependencyInfo);
};

void ProcessMaterialRequest(RenderContext* pRenderContext, const ResourceRegistry::MaterialRequest& materialRequest, BufferResource& stagingBuffer, ImageResource& albedoImage)
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
    Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.second, &pMappedStagingMemory), "Failed to map a pointer to staging memory.");
    {
        // Copy from Host -> Staging memory.
        memcpy(pMappedStagingMemory, pImageData, channels * width * height); // NOLINT

        vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.second);
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

    Check(vmaCreateImage(pRenderContext->GetAllocator(), &imageInfo, &allocInfo, &albedoImage.first, &albedoImage.second, nullptr), "Failed to create dedicated image memory.");

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
    vmaGetAllocationInfo(pRenderContext->GetAllocator(), albedoImage.second, &allocationInfo);

    ImageBarrier(pRenderContext,
                 vkCommand,
                 albedoImage.first,
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
    vkCmdCopyBufferToImage(vkCommand, stagingBuffer.first, albedoImage.first, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &bufferImageCopyInfo);

    ImageBarrier(pRenderContext,
                 vkCommand,
                 albedoImage.first,
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
                        BufferResource&                      stagingBuffer,
                        BufferResource&                      positionBuffer,
                        BufferResource&                      normalBuffer,
                        BufferResource&                      indexBuffer)
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

        BufferResource meshBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &meshBuffer.first, &meshBuffer.second, nullptr), "Failed to create dedicated buffer memory.");

#ifdef _DEBUG
        // Label the allocation.
        vmaSetAllocationName(pRenderContext->GetAllocator(), meshBuffer.second, std::format("Index Buffer Alloc - [{}]", meshRequest.id.GetName()).c_str());

        // Label the buffer object.
        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(meshBuffer.first), std::format("Vertex Buffer - [{}]", meshRequest.id.GetName()));
#endif
        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedData = nullptr;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.second, &pMappedData), "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, dataSize);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.second);
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
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), meshBuffer.second, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0U;
            copyInfo.dstOffset = 0U;
            copyInfo.size      = dataSize;
        }
        vkCmdCopyBuffer(vkCommand, stagingBuffer.first, meshBuffer.first, 1U, &copyInfo);

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
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty())
        return;

    // Create a block of staging buffer memory.
    BufferResource stagingBuffer;
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 256mb.
        bufferInfo.size = static_cast<VkDeviceSize>(256U * 1024U * 1024U);

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        Check(vmaCreateBuffer(m_RenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.first, &stagingBuffer.second, nullptr), "Failed to create staging buffer memory.");
    }

    // Commit Mesh Requests
    // ----------------------------------------------------------

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        const uint64_t& i = 3U * meshRequest.first;
        ProcessMeshRequest(m_RenderContext, meshRequest.second, stagingBuffer, m_BufferResources.at(i + 0U), m_BufferResources.at(i + 1U), m_BufferResources.at(i + 2U));

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
    vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.first, stagingBuffer.second);
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (uint32_t bufferIndex = 0U; bufferIndex < 3U * m_MeshCounter; bufferIndex++)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), m_BufferResources.at(bufferIndex).first, m_BufferResources.at(bufferIndex).second);

    for (uint32_t imageIndex = 0U; imageIndex < 1U * m_MaterialCounter; imageIndex++)
        vmaDestroyImage(m_RenderContext->GetAllocator(), m_ImageResources.at(imageIndex).first, m_ImageResources.at(imageIndex).second);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, BufferResource& positionBuffer, BufferResource& normalBuffer, BufferResource& indexBuffer)
{
    positionBuffer = m_BufferResources.at(3U * resourceHandle + 0U);
    normalBuffer   = m_BufferResources.at(3U * resourceHandle + 1U);
    indexBuffer    = m_BufferResources.at(3U * resourceHandle + 2U);

    return true;
}

bool ResourceRegistry::GetMaterialResources(uint64_t resourceHandle, ImageResource& albedoImage) { return false; }
