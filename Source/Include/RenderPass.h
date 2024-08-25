#ifndef RENDER_PASS_H
#define RENDER_PASS_H

class RenderDelegate;

#include <Common.h>

enum ShaderID
{
    FullscreenTriangleVert,
    VisibilityFrag,
    MeshVert
};

struct VisibilityPushConstants
{
    GfMatrix4f MatrixMVP;
    uint32_t   MeshID;
    uint32_t   MeshCount;
};

class RenderPass final : public HdRenderPass
{
public:

    RenderPass(HdRenderIndex* pRenderIndex, const HdRprimCollection& collection, RenderDelegate* pRenderDelegate);
    ~RenderPass() override;

protected:

    void _Execute(const HdRenderPassStateSharedPtr& pRenderPassState, const TfTokenVector& renderTags) override;

private:

    RenderDelegate* m_Owner;

    Image m_ColorAttachment {};
    Image m_DepthAttachment {};

    VisibilityPushConstants m_VisibilityPushConstants {};

    std::vector<VkVertexInputBindingDescription2EXT>   m_VertexInputBindings;
    std::vector<VkVertexInputAttributeDescription2EXT> m_VertexInputAttributes;

    std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;

    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, VkDescriptorSet> m_MaterialDescriptors;
    std::unordered_map<uint64_t, VkDescriptorSet> m_MeshDataDescriptors;
    std::unordered_map<ShaderID, VkShaderEXT>     m_ShaderMap;

    VkSampler m_DefaultSampler;
};

#endif
