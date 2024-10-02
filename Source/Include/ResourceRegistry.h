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

struct DeviceMaterial
{
    size_t hash;
    Image  albedo;
};

struct DrawItemMetaData
{
    GfMatrix4f matrix;
    uint32_t   faceCount;
    uint32_t   materialIndex;
    GfVec2i    unused;
};

struct ImageData
{
    void*    data;
    uint32_t stride;
    GfVec2i  dim;
    VkFormat format;
};

struct DrawItemRequest
{
    Mesh* pMesh;

    void*  pIndexBufferHost;
    size_t indexBufferSize;

    void*  pVertexBufferHost;
    size_t vertexBufferSize;
};

struct MaterialRequest
{
    Material* pMaterial;

    ImageData albedo;
};

class ResourceRegistry : public HdResourceRegistry
{
public:

    explicit ResourceRegistry(RenderContext* pRenderContext);

    ~ResourceRegistry() noexcept override {};

    void PushDrawItemRequest(DrawItemRequest& request);
    void PushMaterialRequest(MaterialRequest& request);

    inline const std::vector<DrawItem>& GetDrawItems() { return m_DrawItems; }
    inline bool                         IsBusy() { return m_CommitTaskBusy.load(); }

    inline const VkDescriptorSetLayout& GetDrawItemDataDescriptorLayout() { return m_DrawItemDataDescriptorLayout; }
    inline const VkDescriptorSet&       GetDrawItemDataDescriptorSet() { return m_DrawItemDataDescriptorSet; }

    inline const VkDescriptorSetLayout& GetMaterialDataDescriptorLayout() { return m_MaterialDataDescriptorLayout; }
    inline const VkDescriptorSet&       GetMaterialDataDescriptorSet() { return m_MaterialDataDescriptorSet; }

protected:

    void _Commit() override;
    void _GarbageCollect() override;

private:

    void BuildDescriptors();

    RenderContext* m_RenderContext;

    std::atomic<bool> m_CommitTaskBusy;
    tbb::task_group   m_CommitTask;

    std::queue<DrawItemRequest> m_DrawItemRequests;
    std::vector<DrawItem>       m_DrawItems;

    std::queue<MaterialRequest> m_MaterialRequests;
    std::vector<DeviceMaterial> m_DeviceMaterials;

    Buffer m_DrawItemMetaDataBuffer;

    // Using VK_EXT_descriptor_indexing to bind all resource arrays to PSO.
    VkDescriptorSetLayout m_DrawItemDataDescriptorLayout;
    VkDescriptorSet       m_DrawItemDataDescriptorSet;

    VkDescriptorSetLayout m_MaterialDataDescriptorLayout;
    VkDescriptorSet       m_MaterialDataDescriptorSet;

    std::mutex           m_HostBufferPoolMutex;
    uint64_t             m_HostBufferPoolSize;
    std::vector<char8_t> m_HostBufferPool;

    std::mutex           m_HostImagePoolMutex;
    uint64_t             m_HostImagePoolSize;
    std::vector<char8_t> m_HostImagePool;
};

#endif
