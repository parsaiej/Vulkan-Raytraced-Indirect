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

    struct MeshResources
    {
        Buffer indices;
        Buffer positions;
        Buffer normals;
        Buffer texCoords;
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

    bool GetMeshResources(uint64_t resourceHandle, MeshResources& meshResources);
    bool GetMaterialResources(uint64_t resourceHandle, MaterialResources& materialResources);

    inline bool IsBusy() { return m_CommitJobBusy.load(); }

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

    std::array<Buffer, kMaxBufferResources> m_BufferResources;
    std::array<Image, kMaxImageResources>   m_ImageResources;

    RenderContext* m_RenderContext;

    std::queue<MeshRequest>     m_PendingMeshRequests;
    std::queue<MaterialRequest> m_PendingMaterialRequests;

    std::map<uint64_t, MeshResources>     m_MeshResourceMap;
    std::map<uint64_t, MaterialResources> m_MaterialResourceMap;

    // Descriptor Sets
    std::vector<VkDescriptorSet> m_DescriptorSets;

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
};

#endif
