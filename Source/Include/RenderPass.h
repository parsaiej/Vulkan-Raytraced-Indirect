#ifndef RENDER_PASS_H
#define RENDER_PASS_H

class RenderDelegate;
class Scene;
class ResourceRegistry;

#include <Common.h>

enum ShaderID
{
    FullscreenTriangleVert,
    VisibilityFrag,
    MeshVert,
    GBufferResolveComp
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

    // Generic resources
    // ---------------------------------------

    struct RenderPassContext
    {
        RenderContext*     pRenderContext;
        FrameParams*       pFrame;
        HdRenderPassState* pPassState;
        Scene*             pScene;
        ResourceRegistry*  pResourceRegistry;
    };

    RenderDelegate* m_Owner;

    Image m_ColorAttachment {};
    Image m_DepthAttachment {};

    void LoadShader(ShaderID shaderID, const char* filePath, const char* entryName, VkShaderCreateInfoEXT vkShaderInfo);
    std::unordered_map<ShaderID, VkShaderEXT> m_ShaderMap;

    VkSampler m_DefaultSampler;

    // Visibility Pass
    // ---------------------------------------

    Image m_VisibilityBuffer {};

    VkPipelineLayout m_VisibilityPipelineLayout;

    VisibilityPushConstants m_VisibilityPushConstants {};

    std::vector<VkVertexInputBindingDescription2EXT>   m_VertexInputBindings;
    std::vector<VkVertexInputAttributeDescription2EXT> m_VertexInputAttributes;

    void VisibilityPassCreate(RenderContext* pRenderContext);
    void VisibilityPassExecute(RenderPassContext* pCtx);

    // Material Pixel Pass
    // ---------------------------------------

    Buffer m_MaterialCountBuffer {};
    Buffer m_MaterialOffsetBuffer {};
    Buffer m_MaterialPixelBuffer {};

    void MaterialPassCreate(RenderContext* pRenderContext);
    void MaterialPassExecute();

    // GBuffer Pass
    // ---------------------------------------

    struct GBuffer
    {
        Image albedo {};
        Image normal {};
    };

    GBuffer m_GBuffer;
};

#endif
