#include <ResourceRegistry.h>
#include <RenderContext.h>
#include <Mesh.h>
#include <Common.h>

void ProcessMeshRequest(RenderContext* pRenderContext, ResourceRegistry::MeshRequest meshRequest, Buffer& vertexBuffer, Buffer& indexBuffer)
{
    spdlog::info("Processing Mesh Request for {}", meshRequest.id.GetName());

    std::pair<VkBuffer, VmaAllocation> stagingMemoryIndices;
    std::pair<VkBuffer, VmaAllocation> stagingMemoryVertices;

    auto UploadHostMemoryToStagingBuffer = [&](const void* pData, uint32_t size)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        
        std::pair<VkBuffer, VmaAllocation> stagingBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.first, &stagingBuffer.second, nullptr), "Failed to create staging buffer memory.");

        void* pMappedData;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.second, &pMappedData), "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, size);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.second);
        }

        return stagingBuffer;
    };

    auto AllocateDedicatedBuffer = [&](uint32_t size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = size;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        
        std::pair<VkBuffer, VmaAllocation> dedicatedBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &dedicatedBuffer.first, &dedicatedBuffer.second, nullptr), "Failed to create staging buffer memory.");

        return dedicatedBuffer;
    };

    // Staging Memory.
    stagingMemoryIndices  = UploadHostMemoryToStagingBuffer(meshRequest.pIndices.data(), sizeof(GfVec3i) * (uint32_t)meshRequest.pIndices.size());
    stagingMemoryVertices = UploadHostMemoryToStagingBuffer(meshRequest.pPoints.data(),  sizeof(GfVec3f)  * (uint32_t)meshRequest.pPoints.size());

#ifdef _DEBUG
    vmaSetAllocationName(pRenderContext->GetAllocator(), stagingMemoryIndices.second,  std::format("Staging Index Buffer - [{}]", meshRequest.id.GetName()).c_str());
    vmaSetAllocationName(pRenderContext->GetAllocator(), stagingMemoryVertices.second, std::format("Staging Vertex Buffer - [{}]",  meshRequest.id.GetName()).c_str());
#endif

    // Dedicated Memory.
    indexBuffer  = AllocateDedicatedBuffer(sizeof(GfVec3i) * (uint32_t)meshRequest.pIndices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    vertexBuffer = AllocateDedicatedBuffer(sizeof(GfVec3f) * (uint32_t)meshRequest.pPoints.size(),  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

#ifdef _DEBUG
    vmaSetAllocationName(pRenderContext->GetAllocator(), indexBuffer.second,  std::format("Index Buffer - [{}]", meshRequest.id.GetName()).c_str());
    vmaSetAllocationName(pRenderContext->GetAllocator(), vertexBuffer.second, std::format("Vertex Buffer - [{}]",  meshRequest.id.GetName()).c_str());
#endif

    // Transfer Staging -> Dedicated Memory.

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

    auto RecordCopy = [&](std::pair<VkBuffer, VmaAllocation>& src, std::pair<VkBuffer, VmaAllocation>& dst)
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), src.second, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0u;
            copyInfo.dstOffset = 0u;
            copyInfo.size      = allocationInfo.size;
        }
        vkCmdCopyBuffer(vkCommand, src.first, dst.first, 1u, &copyInfo);
    };

    RecordCopy(stagingMemoryIndices,  indexBuffer);
    RecordCopy(stagingMemoryVertices, vertexBuffer);

    Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

    VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    {
        vkSubmitInfo.commandBufferCount = 1u;
        vkSubmitInfo.pCommandBuffers    = &vkCommand;
    }
    Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1u, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

    // Wait for the copies to complete. 
    Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for copy commands to finish dispatching.");

    // Release staging memory. 
    vmaDestroyBuffer(pRenderContext->GetAllocator(), stagingMemoryIndices .first, stagingMemoryIndices .second);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), stagingMemoryVertices.first, stagingMemoryVertices.second);
}

void ResourceRegistry::_Commit()
{
    if (m_PendingMeshRequests.empty())
        return;

    while (!m_PendingMeshRequests.empty())
    {
        auto meshRequest = m_PendingMeshRequests.front();

        Buffer vertexBuffer, indexBuffer;
        ProcessMeshRequest(m_RenderContext, meshRequest.second, vertexBuffer, indexBuffer);

        m_VertexBuffers[meshRequest.first] = vertexBuffer;
        m_IndexBuffers [meshRequest.first] = indexBuffer;

        // Request process, remove. 
        m_PendingMeshRequests.pop();
    }
}

void ResourceRegistry::_GarbageCollect()
{
    vkDeviceWaitIdle(m_RenderContext->GetDevice());

    for (auto& buffer : m_VertexBuffers)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), buffer.second.first, buffer.second.second);

    for (auto& buffer : m_IndexBuffers)
        vmaDestroyBuffer(m_RenderContext->GetAllocator(), buffer.second.first, buffer.second.second);
}

bool ResourceRegistry::GetMeshResources(uint64_t resourceHandle, Buffer& vertexBuffer, Buffer& indexBuffer)
{
    vertexBuffer = m_VertexBuffers[resourceHandle];
    indexBuffer  = m_IndexBuffers[resourceHandle];

    return true;
}