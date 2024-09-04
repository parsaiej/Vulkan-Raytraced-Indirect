#ifndef RESOURCE_REGISTRY_H
#define RESOURCE_REGISTRY_H

class Mesh;
class RenderContext;

#include <Common.h>

class ResourceRegistry : public HdResourceRegistry
{
public:

    struct MeshRequest
    {
        SdfPath      id;
        SdfPath      materialId;
        VtVec3fArray pPoints;
        VtVec3iArray pTriangles;
    };

    struct MaterialRequest
    {
        SdfPath      id;
        SdfAssetPath imagePathAlbedo;
        SdfAssetPath imagePathNormal;
        SdfAssetPath imagePathRoughness;
        SdfAssetPath imagePathMetallic;
    };

    struct MeshResources
    {
        Buffer   indices;
        Buffer   positions;
        uint32_t materialResourceIndex {};
    };

    struct MaterialResources
    {
        Image albedo;
        Image normal;
        Image metallic;
        Image roughness;
    };

    // Queues a GPU-upload request for vertex and index mesh buffers.
    inline uint64_t PushMeshRequest(MeshRequest meshRequest)
    {
        m_PendingMeshRequests.emplace(meshRequest);
        return meshRequest.id.GetHash();
    }

    inline uint64_t PushMaterialRequest(MaterialRequest materialRequest)
    {
        m_PendingMaterialRequests.emplace(materialRequest);
        return materialRequest.id.GetHash();
    }

    inline bool IsBusy() { return m_CommitJobBusy.load(); }
    inline bool IsComplete() { return m_CommitJobComplete.load(); }

    inline const VkDescriptorSetLayout& GetResourceDescriptorLayout() { return m_ResourceRegistryDescriptorSetLayout; }
    inline const VkDescriptorSet&       GetResourceDescriptorSet() { return m_ResourceRegistryDescriptorSet; }

    explicit ResourceRegistry(RenderContext* pRenderContext);

protected:

    void _Commit() override;
    void _GarbageCollect() override;

private:

    void CommitJob();

    uint32_t m_BufferCounter {};
    uint32_t m_ImageCounter {};

    const static uint32_t kMaxBufferResources = 16U * 8192U;
    const static uint32_t kMaxImageResources  = 512U;

    RenderContext* m_RenderContext;

    std::queue<MeshRequest>     m_PendingMeshRequests;
    std::queue<MaterialRequest> m_PendingMaterialRequests;

    std::vector<MeshResources>     m_MeshResources;
    std::vector<MaterialResources> m_MaterialResources;

    // The backing resources for Mesh/Materials.
    std::array<Buffer, kMaxBufferResources> m_BufferResources;
    std::array<Image, kMaxImageResources>   m_ImageResources;

    // Using VK_EXT_descriptor_indexing to bind all resource arrays to PSO.
    VkDescriptorSetLayout m_ResourceRegistryDescriptorSetLayout;
    VkDescriptorSet       m_ResourceRegistryDescriptorSet;

    void ProcessMeshRequest(RenderContext*                       pRenderContext,
                            const ResourceRegistry::MeshRequest& meshRequest,
                            Buffer&                              stagingBuffer,
                            ResourceRegistry::MeshResources*     pMesh);

    void ProcessMaterialRequest(RenderContext*                           pRenderContext,
                                const ResourceRegistry::MaterialRequest& materialRequest,
                                Buffer&                                  stagingBuffer,
                                ResourceRegistry::MaterialResources*     pMaterial);

    VkCommandPool m_ResourceCreationCommandPool;

    std::jthread      m_CommitJobThread;
    std::atomic<bool> m_CommitJobBusy;
    std::atomic<bool> m_CommitJobComplete;

    // Descriptor Set
};

#endif
