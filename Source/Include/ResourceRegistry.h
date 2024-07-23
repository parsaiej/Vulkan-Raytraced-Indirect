#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

class Mesh;
class RenderContext;

#include <queue>

typedef std::pair<VkBuffer, VmaAllocation> Buffer;

class ResourceRegistry : public HdResourceRegistry
{
public:

    struct MeshRequest
    {
        SdfPath      id;
        VtVec3fArray pPoints;
        VtVec3iArray pIndices;
    };

    // Queues a GPU-upload request for vertex and index mesh buffers. 
    inline uint64_t PushMeshRequest(MeshRequest meshRequest) { m_PendingMeshRequests.push({ m_ResourceCounter, meshRequest} ); return m_ResourceCounter++; }

    bool GetMeshResources(uint64_t resourceHandle, Buffer& vertexBuffer, Buffer& indexBuffer);

    ResourceRegistry(RenderContext* pRenderContext) : m_RenderContext(pRenderContext), m_ResourceCounter(0u) {}

protected:
    void _Commit() override;
    void _GarbageCollect() override;

private:
    RenderContext* m_RenderContext;

    // Resources. 
    std::map<uint64_t, Buffer> m_VertexBuffers;
    std::map<uint64_t, Buffer> m_IndexBuffers;

    // Queue of resource creation requests that are made during the sync phase.
    std::queue<std::pair<uint64_t, MeshRequest>> m_PendingMeshRequests; 

    uint64_t m_ResourceCounter;
};

#endif