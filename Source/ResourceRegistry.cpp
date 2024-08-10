#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>

void ProcessMeshRequest(RenderContext* pRenderContext, ResourceRegistry::MeshRequest meshRequest,
    BufferResource& stagingBuffer, BufferResource& positionBuffer, BufferResource& normalBuffer,
    BufferResource& indexBuffer)
{
    spdlog::info("Processing Mesh Request for {}", meshRequest.id.GetName());

    auto CreateMeshBuffer = [&](const void* pData, uint32_t dataSize, VkBufferUsageFlags usage) {
        // Create dedicate device memory for the mesh buffer.
        // -----------------------------------------------------

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = dataSize;
        bufferInfo.usage              = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        BufferResource meshBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo,
                  &meshBuffer.first, &meshBuffer.second, nullptr),
            "Failed to create staging buffer memory.");

#ifdef _DEBUG
        // Label the allocation.
        vmaSetAllocationName(pRenderContext->GetAllocator(), meshBuffer.second,
            std::format("Index Buffer Alloc - [{}]", meshRequest.id.GetName()).c_str());

        // Label the buffer object.
        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_BUFFER,
            (uint64_t)meshBuffer.first,
            std::format("Vertex Buffer - [{}]", meshRequest.id.GetName()));
#endif
        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedData;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.second, &pMappedData),
            "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, dataSize);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.second);
        }

        // Copy Staging -> Device Memory.
        // -----------------------------------------------------

        VkCommandBufferAllocateInfo vkCommandAllocateInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        {
            vkCommandAllocateInfo.commandBufferCount = 1u;
            vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
        }

        VkCommandBuffer vkCommand;
        Check(vkAllocateCommandBuffers(
                  pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommand),
            "Failed to created command buffer for uploading scene resource "
            "memory.");

        VkCommandBufferBeginInfo vkCommandsBeginInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        {
            vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo),
            "Failed to begin recording upload commands");

        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), meshBuffer.second, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0u;
            copyInfo.dstOffset = 0u;
            copyInfo.size      = dataSize;
        }
        vkCmdCopyBuffer(vkCommand, stagingBuffer.first, meshBuffer.first, 1u, &copyInfo);

        Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

        VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            vkSubmitInfo.commandBufferCount = 1u;
            vkSubmitInfo.pCommandBuffers    = &vkCommand;
        }
        Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1u, &vkSubmitInfo, VK_NULL_HANDLE),
            "Failed to submit copy commands to the graphics queue.");

        // Wait for the copy to complete.
        // -----------------------------------------------------

        Check(vkDeviceWaitIdle(pRenderContext->GetDevice()),
            "Failed to wait for copy commands to finish dispatching.");

        return meshBuffer;
    };

    indexBuffer    = CreateMeshBuffer(meshRequest.pIndices.data(),
           sizeof(GfVec3i) * (uint32_t)meshRequest.pIndices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    positionBuffer = CreateMeshBuffer(meshRequest.pPoints.data(),
        sizeof(GfVec3f) * (uint32_t)meshRequest.pPoints.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    normalBuffer   = CreateMeshBuffer(meshRequest.pNormals.data(),
          sizeof(GfVec3f) * (uint32_t)meshRequest.pNormals.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty())
        return;

    // Commit Mesh Requests
    // ----------------------------------------------------------

    // Create a block of staging buffer memory.
    BufferResource stagingBuffer;
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 256mb.
        bufferInfo.size = 256u * 1024u * 1024u;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        Check(vmaCreateBuffer(m_RenderContext->GetAllocator(), &bufferInfo, &allocInfo,
                  &stagingBuffer.first, &stagingBuffer.second, nullptr),
            "Failed to create staging buffer memory.");
    }

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        const uint64_t& i = 3u * meshRequest.first;
        ProcessMeshRequest(m_RenderContext, meshRequest.second, stagingBuffer,
            m_BufferResources[i + 0u], m_BufferResources[i + 1u], m_BufferResources[i + 2u]);

        // Request process, remove.
        m_PendingMeshRequests.pop();
    }

    // Release staging memory.
    vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.first, stagingBuffer.second);

    // Commit Material Requests
    // -----------------------------------------------------------

    ImageResource stagingImage;
    {
        VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.usage             = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 8x8k SRGB.
        imageInfo.extent      = { 8192u, 8192u, 1u };
        imageInfo.format      = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.mipLevels   = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.imageType   = VK_IMAGE_TYPE_2D;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        Check(vmaCreateImage(m_RenderContext->GetAllocator(), &imageInfo, &allocInfo,
                  &stagingImage.first, &stagingImage.second, nullptr),
            "Failed to create staging image memory.");
    }

    while (!m_PendingMaterialRequests.empty())
    {
        auto materialRequest = m_PendingMaterialRequests.front();

        spdlog::info("Processing Material Request for {}", materialRequest.second.id.GetName());

        m_PendingMaterialRequests.pop();
    }

    vmaDestroyImage(m_RenderContext->GetAllocator(), stagingImage.first, stagingBuffer.second);
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (uint32_t bufferIndex = 0u; bufferIndex < 3u * m_MeshCounter; bufferIndex++)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), m_BufferResources[bufferIndex].first,
            m_BufferResources[bufferIndex].second);

    // for (uint32_t imageIndex = 0u; imageIndex < m_ImageCounter; imageIndex++)
    //     vmaDestroyImage(m_RenderContext->GetAllocator(),
    //     m_ImageResources[imageIndex].first,
    //     m_ImageResources[imageIndex].second);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, BufferResource& positionBuffer,
    BufferResource& normalBuffer, BufferResource& indexBuffer)
{
    positionBuffer = m_BufferResources[3u * resourceHandle + 0u];
    normalBuffer   = m_BufferResources[3u * resourceHandle + 1u];
    indexBuffer    = m_BufferResources[3u * resourceHandle + 2u];

    return true;
}