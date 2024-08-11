#ifndef RENDER_PASS_H
#define RENDER_PASS_H

class RenderDelegate;

#include <Common.h>

enum ShaderID
{
    FullscreenTriangleVert,
    LitFrag,
    MeshVert
};

class RenderPass final : public HdRenderPass
{
public:
    RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection, RenderDelegate* pRenderDelegate);
    ~RenderPass() override;

protected:
    void _Execute(HdRenderPassStateSharedPtr const& pRenderPassState, TfTokenVector const& renderTags) override;

private:
    RenderDelegate* m_Owner;

    Image m_ColorAttachment {};
    Image m_DepthAttachment {};

    PushConstants m_PushConstants {};

    std::vector<VkVertexInputBindingDescription2EXT> m_VertexInputBindings;
    std::vector<VkVertexInputAttributeDescription2EXT> m_VertexInputAttributes;

    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout           = VK_NULL_HANDLE;

    std::unordered_map<ShaderID, VkShaderEXT> m_ShaderMap;
};

#endif