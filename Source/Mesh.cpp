#include <Mesh.h>
#include <RenderDelegate.h>
#include <RenderContext.h>
#include <Scene.h>
#include <Common.h>

Mesh::Mesh(SdfPath const& rprimId, RenderDelegate* pRenderDelegate) : HdMesh(rprimId), m_Owner(pRenderDelegate)
{
    
}

Mesh::~Mesh()
{

} 

HdDirtyBits Mesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void Mesh::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits, TfToken const& reprToken)
{
    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());
 
    SdfPath id = GetId();

    auto pPointList = pSceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();

    VtVec3iArray trianglesFaceVertexIndices;

    // Compute the triangulated indices from the mesh topology.
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(id);
    HdMeshUtil meshUtil(&topology, id);
    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&trianglesFaceVertexIndices, &trianglePrimitiveParams);

    std::pair<VkBuffer, VmaAllocation> stagingMemoryIndices;
    std::pair<VkBuffer, VmaAllocation> stagingMemoryVertices;

    auto pAllocator = m_Owner->GetRenderContext()->GetAllocator();

    auto UploadHostMemoryToStagingBuffer = [&](const void* pData, uint32_t size)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        
        std::pair<VkBuffer, VmaAllocation> stagingBuffer;
        Check(vmaCreateBuffer(pAllocator, &bufferInfo, &allocInfo, &stagingBuffer.first, &stagingBuffer.second, nullptr), "Failed to create staging buffer memory.");

        void* pMappedData;
        Check(vmaMapMemory(pAllocator, stagingBuffer.second, &pMappedData), "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, size);

            vmaUnmapMemory(pAllocator, stagingBuffer.second);
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
        Check(vmaCreateBuffer(pAllocator, &bufferInfo, &allocInfo, &dedicatedBuffer.first, &dedicatedBuffer.second, nullptr), "Failed to create staging buffer memory.");

        return dedicatedBuffer;
    };

    // Staging Memory.
    stagingMemoryIndices  = UploadHostMemoryToStagingBuffer(trianglesFaceVertexIndices.data(),  sizeof(uint32_t) * (uint32_t)trianglesFaceVertexIndices.size());
    stagingMemoryVertices = UploadHostMemoryToStagingBuffer(pPointList.data(), sizeof(Vertex)   * (uint32_t)pPointList.size());

    // Dedicated Memory.
    m_IndexBuffer  = AllocateDedicatedBuffer(sizeof(uint32_t) * (uint32_t)trianglesFaceVertexIndices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m_VertexBuffer = AllocateDedicatedBuffer(sizeof(Vertex)   * (uint32_t)pPointList.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // Transfer Staging -> Dedicated Memory.

    VkCommandBufferAllocateInfo vkCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    {
        vkCommandAllocateInfo.commandBufferCount = 1u;
        vkCommandAllocateInfo.commandPool        = m_Owner->GetRenderContext()->GetCommandPool();
    }

    VkCommandBuffer vkCommand;
    Check(vkAllocateCommandBuffers(m_Owner->GetRenderContext()->GetDevice(), &vkCommandAllocateInfo, &vkCommand), "Failed to created command buffer for uploading scene resource memory.");

    VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    {
        vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo), "Failed to begin recording upload commands");

    auto RecordCopy = [&](std::pair<VkBuffer, VmaAllocation>& src, std::pair<VkBuffer, VmaAllocation>& dst)
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pAllocator, src.second, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0u;
            copyInfo.dstOffset = 0u;
            copyInfo.size      = allocationInfo.size;
        }
        vkCmdCopyBuffer(vkCommand, src.first, dst.first, 1u, &copyInfo);
    };

    RecordCopy(stagingMemoryIndices,  m_IndexBuffer);
    RecordCopy(stagingMemoryVertices, m_VertexBuffer);

    Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

    VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    {
        vkSubmitInfo.commandBufferCount = 1u;
        vkSubmitInfo.pCommandBuffers    = &vkCommand;
    }
    Check(vkQueueSubmit(m_Owner->GetRenderContext()->GetCommandQueue(), 1u, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

    // Wait for the copies to complete. 
    Check(vkDeviceWaitIdle(m_Owner->GetRenderContext()->GetDevice()), "Failed to wait for copy commands to finish dispatching.");

    // Release staging memory. 
    vmaDestroyBuffer(pAllocator, stagingMemoryIndices .first, stagingMemoryIndices .second);
    vmaDestroyBuffer(pAllocator, stagingMemoryVertices.first, stagingMemoryVertices.second);

    m_Owner->GetRenderContext()->GetScene()->AddMesh(this);
}

HdDirtyBits Mesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void Mesh::_InitRepr(TfToken const& reprToken, HdDirtyBits*)
{
    _ReprVector::iterator it = std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));

    if (it == _reprs.end())
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
}

void Mesh::Finalize(HdRenderParam* /* renderParam */) {}