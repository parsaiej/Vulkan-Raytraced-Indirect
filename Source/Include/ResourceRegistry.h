#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

class Mesh;
class Material;
class RenderContext;

#include <Common.h>

struct DrawItem
{
    Mesh* pMesh = nullptr;

    uint32_t indexCount;

    Buffer bufferI;
    Buffer bufferV;
};

struct DrawItemRequest
{
    Mesh* pMesh;

    void*  pIndexBufferHost;
    size_t indexBufferSize;

    void*  pVertexBufferHost;
    size_t vertexBufferSize;
};

class ResourceRegistry : public HdResourceRegistry
{
public:

    explicit ResourceRegistry(RenderContext* pRenderContext);

    ~ResourceRegistry() noexcept override {};

    // Maps a pointer to the allocated memory chunk for the mesh to copy to.
    void AddMesh(Mesh* pMesh, uint64_t bufferSizeI, uint64_t bufferSizeV, void** ppBufferI, void** ppBufferV);

    inline const std::vector<DrawItem>& GetDrawItems() { return m_DrawItems; }
    inline bool                         IsBusy() { return m_CommitTaskBusy.load(); }

protected:

    void _Commit() override;
    void _GarbageCollect() override;

private:

    std::mutex m_MeshAllocationMutex;

    RenderContext* m_RenderContext;

    std::atomic<bool> m_CommitTaskBusy;
    tbb::task_group   m_CommitTask;

    std::queue<DrawItemRequest> m_DrawItemRequests;

    std::vector<DrawItem> m_DrawItems;

    std::atomic<uint64_t> m_HostBufferPoolSizeI;
    std::atomic<uint64_t> m_HostBufferPoolSizeV;

    std::array<char8_t, kIndicesPoolMaxBytes>  m_HostBufferPoolI;
    std::array<char8_t, kVerticesPoolMaxBytes> m_HostBufferPoolV;
};

#endif
