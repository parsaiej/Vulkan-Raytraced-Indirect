#ifndef RENDER_PASS_H
#define RENDER_PASS_H

class RenderDelegate;
class ResourceRegistry;

#include <Common.h>

enum ShaderID
{
    VisibilityVert,
    VisibilityFrag,
    DebugVert,
    DebugFrag,
    GBufferResolveComp
};

struct VisibilityPushConstants
{
    GfMatrix4f MatrixMVP;
    uint32_t   MeshID;
    uint32_t   MeshCount;
};

struct DebugPushConstants
{
    GfMatrix4f MatrixVP;
    uint32_t   DebugModeValue;
    uint32_t   MeshCount;
};

class RenderPass final : public HdRenderPass
{
public:

    enum DebugMode
    {
        None,
        MeshID,
        PrimitiveID,
        BarycentricCoordinate,
        Depth,
        Albedo,
        Brixelizer
    };

    RenderPass(HdRenderIndex* pRenderIndex, const HdRprimCollection& collection, RenderDelegate* pRenderDelegate);
    ~RenderPass() override;

protected:

    void _Execute(const HdRenderPassStateSharedPtr& pRenderPassState, const TfTokenVector& renderTags) override;

private:

    // Generic resources
    // ---------------------------------------

    struct FrameContext
    {
        RenderContext*               pRenderContext;
        FrameParams*                 pFrame;
        HdRenderPassState*           pPassState;
        ResourceRegistry*            pResourceRegistry;
        DebugMode                    debugMode;
        FfxBrixelizerTraceDebugModes debugModeBrixelizer;
    };

    RenderDelegate* m_Owner;

    Image m_ColorAttachment {};
    Image m_DepthAttachment {};

    void LoadShader(ShaderID shaderID, const char* filePath, const char* entryName, VkShaderCreateInfoEXT vkShaderInfo);
    std::unordered_map<ShaderID, VkShaderEXT> m_ShaderMap;

    VkSampler m_DefaultSampler;

    // FidelityFX Primitives
    // ---------------------------------------

    bool m_RebuildAccelerationStructure { true };

    void RebuildAccelerationStructure(FrameContext* pFrameContext);
    void CreateBrixelizerLatentDeviceResources();

    FfxDevice            m_FFXDevice {};
    FfxInterface         m_FFXInterface {};
    std::vector<uint8_t> m_FFXBackendScratch;

    // Latent buffers required by Brixelizer:
    std::pair<FfxResource, Image>               m_FFXBrixelizerBufferSDFAtlas;
    std::pair<FfxResource, Buffer>              m_FFXBrixelizerBufferBrickAABB;
    std::pair<FfxResource, Buffer>              m_FFXBrixelizerBufferDeviceScratch;
    std::vector<std::pair<FfxResource, Buffer>> m_FFXBrixelizerBufferPerCascadeAABBTree;
    std::vector<std::pair<FfxResource, Buffer>> m_FFXBrixelizerBufferPerCascadeBrickMap;

    // Debug output.
    FfxResource m_FFXBrixelizerDebugOutput;

    uint32_t             m_FFXDeviceScratchSizeBytes {};
    uint32_t             m_FFXBrixelizerCascadeCount {};
    FfxBrixelizerContext m_FFXBrixelizerContext {};

    // This type is stackoverflow-prone due to its gigantic size, put it on the heap.
    std::unique_ptr<FfxBrixelizerBakedUpdateDescription> m_FFXBrixelizerBakedUpdateDesc;

    // Resource Descriptors
    // ---------------------------------------

    // Visibility Pass
    // ---------------------------------------

    Image m_VisibilityBuffer {};

    VkPipelineLayout m_VisibilityPipelineLayout;

    VisibilityPushConstants m_VisibilityPushConstants {};

    std::vector<VkVertexInputBindingDescription2EXT>   m_VertexInputBindings;
    std::vector<VkVertexInputAttributeDescription2EXT> m_VertexInputAttributes;

    void VisibilityPassCreate(RenderContext* pRenderContext);
    void VisibilityPassExecute(FrameContext* pFrameContext);

    // Material Pixel Pass
    // ---------------------------------------

    Buffer m_MaterialCountBuffer {};
    Buffer m_MaterialOffsetBuffer {};
    Buffer m_MaterialPixelBuffer {};

    void MaterialPassCreate(RenderContext* pRenderContext);
    void MaterialPassExecute();

    // Debug Pass
    // ---------------------------------------

    VkDescriptorSetLayout m_DebugDescriptorSetLayout;
    VkPipelineLayout      m_DebugPipelineLayout;

    DebugPushConstants m_DebugPushConstants {};

    void DebugPassCreate(RenderContext* pRenderContext);
    void DebugPassExecute(FrameContext* pFrameContext);

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
