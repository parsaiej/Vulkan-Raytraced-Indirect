#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

class Mesh;
class RenderContext;

#include <queue>

using BufferResource = std::pair<VkBuffer, VmaAllocation>;
using ImageResource  = std::pair<VkImage, VmaAllocation>;

class ResourceRegistry : public HdResourceRegistry
{
public:
    struct MeshRequest
    {
        SdfPath id;
        VtVec3fArray pPoints;
        VtVec3fArray pNormals;
        VtVec3iArray pIndices;
    };

    struct MaterialRequest
    {
        SdfPath id;
        SdfAssetPath imagePathAlbedo;
        SdfAssetPath imagePathNormal;
        SdfAssetPath imagePathRoughness;
        SdfAssetPath imagePathMetallic;
    };

    // Queues a GPU-upload request for vertex and index mesh buffers.
    inline uint64_t PushMeshRequest(MeshRequest meshRequest)
    {
        m_PendingMeshRequests.emplace(m_MeshCounter, meshRequest);
        return m_MeshCounter++;
    }

    inline uint64_t PushMaterialRequest(MaterialRequest materialRequest)
    {
        m_PendingMaterialRequests.emplace(m_MaterialCounter, materialRequest);
        return m_MaterialCounter++;
    }

    bool GetMeshResources(uint64_t resourceHandle, BufferResource& positionBuffer, BufferResource& normalBuffer, BufferResource& indexBuffer);

    explicit ResourceRegistry(RenderContext* pRenderContext) : m_RenderContext(pRenderContext) {}

protected:
    void _Commit() override;
    void _GarbageCollect() override;

private:
    RenderContext* m_RenderContext;

    const static uint32_t kMaxBufferResources = 512U;
    const static uint32_t kMaxImageResources  = 512U;

    // Resources.
    std::array<BufferResource, kMaxBufferResources> m_BufferResources;
    std::array<ImageResource, kMaxImageResources> m_ImageResources;

    std::queue<std::pair<uint64_t, MeshRequest>> m_PendingMeshRequests;
    std::queue<std::pair<uint64_t, MaterialRequest>> m_PendingMaterialRequests;

    uint64_t m_MeshCounter {};
    uint64_t m_MaterialCounter {};
};

#endif