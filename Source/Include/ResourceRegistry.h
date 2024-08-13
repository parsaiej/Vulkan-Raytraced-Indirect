#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

class Mesh;
class RenderContext;

#include <Common.h>

#include <queue>

class ResourceRegistry : public HdResourceRegistry
{
public:

    struct MeshRequest
    {
        SdfPath      id;
        VtVec3fArray pPoints;
        VtVec3fArray pNormals;
        VtVec3iArray pIndices;
        VtVec2fArray pTexCoords;
    };

    struct MaterialRequest
    {
        SdfPath      id;
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

    bool GetMeshResources(uint64_t resourceHandle, Buffer& positionBuffer, Buffer& normalBuffer, Buffer& indexBuffer, Buffer& texCoordBuffer);
    bool GetMaterialResources(uint64_t resourceHandle, Image& albedoImage);

    inline VkDescriptorSetLayout* GetDescriptorSetLayout() { return &m_DescriptorSetLayout; }

    explicit ResourceRegistry(RenderContext* pRenderContext) : m_RenderContext(pRenderContext) {}

protected:

    void _Commit() override;
    void _GarbageCollect() override;

private:

    const static uint32_t kMaxBufferResources = 512U;
    const static uint32_t kMaxImageResources  = 512U;

    void SyncDescriptorSets(RenderContext* pRenderContext, const std::array<Image, kMaxImageResources>& imageResources, std::vector<VkDescriptorSet>& descriptorSets);

    RenderContext* m_RenderContext;

    // Resources.
    std::array<Buffer, kMaxBufferResources> m_BufferResources;
    std::array<Image, kMaxImageResources>   m_ImageResources;

    std::queue<std::pair<uint64_t, MeshRequest>>     m_PendingMeshRequests;
    std::queue<std::pair<uint64_t, MaterialRequest>> m_PendingMaterialRequests;

    // Descriptor Sets
    std::vector<VkDescriptorSet> m_DescriptorSets;

    uint64_t m_MeshCounter {};
    uint64_t m_MaterialCounter {};

    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

#endif
