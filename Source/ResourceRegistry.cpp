#include <ResourceRegistry.h>
#include <RenderContext.h>
#include <Mesh.h>
#include <Common.h>

void ProcessMeshRequest(RenderContext* pRenderContext, ResourceRegistry::MeshRequest meshRequest, Buffer& stagingBuffer, Buffer& positionBuffer, Buffer& normalBuffer, Buffer& indexBuffer)
{
    spdlog::info("Processing Mesh Request for {}", meshRequest.id.GetName());

    auto CreateMeshBuffer = [&](const void* pData, uint32_t dataSize, VkBufferUsageFlags usage)
    {
        // Create dedicate device memory for the mesh buffer.
        // -----------------------------------------------------

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = dataSize;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        
        Buffer meshBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &meshBuffer.first, &meshBuffer.second, nullptr), "Failed to create staging buffer memory.");
        
#ifdef _DEBUG
        // Label the allocation.
        vmaSetAllocationName(pRenderContext->GetAllocator(), meshBuffer.second,  std::format("Index Buffer Alloc - [{}]", meshRequest.id.GetName()).c_str());
        
        // Label the buffer object.
        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)meshBuffer.first, std::format("Vertex Buffer - [{}]",  meshRequest.id.GetName()));
#endif
        // Copy Host -> Staging Memory.
        // -----------------------------------------------------
        
        void* pMappedData;
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
            vkCommandAllocateInfo.commandBufferCount = 1u;
            vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
        }

        VkCommandBuffer vkCommand;
        Check(vkAllocateCommandBuffers(pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommand), "Failed to created command buffer for uploading scene resource memory.");

        VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        {
            vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo), "Failed to begin recording upload commands");

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
        Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1u, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

        // Wait for the copy to complete. 
        // -----------------------------------------------------

        Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for copy commands to finish dispatching.");

        return meshBuffer;
    };

    indexBuffer    = CreateMeshBuffer(meshRequest.pIndices.data(), sizeof(GfVec3i) * (uint32_t)meshRequest.pIndices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    positionBuffer = CreateMeshBuffer(meshRequest.pPoints.data(),  sizeof(GfVec3f) * (uint32_t)meshRequest.pPoints.size(),  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    normalBuffer   = CreateMeshBuffer(meshRequest.pNormals.data(), sizeof(GfVec3f) * (uint32_t)meshRequest.pNormals.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty())
        return;

    // Create a block of staging buffer memory. 
    Buffer stagingBuffer;
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 256mb.
        bufferInfo.size  = 256u * 1024u * 1024u;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        
        Check(vmaCreateBuffer(m_RenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.first, &stagingBuffer.second, nullptr), "Failed to create staging buffer memory.");
    }

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        Buffer positionBuffer, normalBuffer, indexBuffer;
        ProcessMeshRequest(m_RenderContext, meshRequest.second, stagingBuffer, positionBuffer, normalBuffer, indexBuffer);

        m_PositionBuffers[meshRequest.first] = positionBuffer;
        m_NormalBuffers  [meshRequest.first] = normalBuffer;
        m_IndexBuffers   [meshRequest.first] = indexBuffer;

        // Request process, remove. 
        m_PendingMeshRequests.pop();
    }
    
    // Release staging memory. 
    vmaDestroyBuffer(m_RenderContext->GetAllocator(), stagingBuffer.first, stagingBuffer.second);
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (auto& buffer : m_PositionBuffers)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), buffer.second.first, buffer.second.second);

    for (auto& buffer : m_NormalBuffers)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), buffer.second.first, buffer.second.second);

    for (auto& buffer : m_IndexBuffers)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), buffer.second.first, buffer.second.second);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, Buffer& positionBuffer, Buffer& normalBuffer, Buffer& indexBuffer)
{
    positionBuffer = m_PositionBuffers[resourceHandle];
    normalBuffer   = m_NormalBuffers  [resourceHandle];
    indexBuffer    = m_IndexBuffers   [resourceHandle];

    return true;
}