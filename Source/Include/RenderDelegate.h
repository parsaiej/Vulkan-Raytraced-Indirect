#ifndef RENDER_DELEGATE_H
#define RENDER_DELEGATE_H

class RenderContext;
class ResourceRegistry;

// USD Hydra Render Delegate
// ---------------------------------------------------------

const TfTokenVector kSupportedRPrimTypes = { HdPrimTypeTokens->mesh };                               // NOLINT
const TfTokenVector kSupportedSPrimTypes = { HdPrimTypeTokens->camera, HdPrimTypeTokens->material }; // NOLINT
const TfTokenVector kSupportedBPrimTypes = {};                                                       // NOLINT

// This token is used for identifying our custom Hydra driver.
const TfToken kTokenRenderContextDriver = TfToken("RenderContextDriver");
const TfToken kTokenCurrenFrameParams   = TfToken("CurrentFrameParams");

class RenderDelegate : public HdRenderDelegate
{
public:

    RenderDelegate() = default;
    explicit RenderDelegate(const HdRenderSettingsMap& settingsMap) {};
    ~RenderDelegate() override = default;

    void SetDrivers(const HdDriverVector& drivers) override;

    [[nodiscard]] const TfTokenVector& GetSupportedRprimTypes() const override { return kSupportedRPrimTypes; };
    [[nodiscard]] const TfTokenVector& GetSupportedSprimTypes() const override { return kSupportedSPrimTypes; };
    [[nodiscard]] const TfTokenVector& GetSupportedBprimTypes() const override { return kSupportedBPrimTypes; };

    [[nodiscard]] HdResourceRegistrySharedPtr GetResourceRegistry() const override { return m_ResourceRegistry; };

    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index, const HdRprimCollection& collection) override;

    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, const SdfPath& id) override { return new HdInstancer(delegate, id); };
    void         DestroyInstancer(HdInstancer* instancer) override {};

    HdRprim* CreateRprim(const TfToken& typeId, const SdfPath& rprimId) override;
    HdSprim* CreateSprim(const TfToken& typeId, const SdfPath& sprimId) override;
    HdBprim* CreateBprim(const TfToken& typeId, const SdfPath& bprimId) override { return nullptr; };

    HdSprim* CreateFallbackSprim(const TfToken& /*typeId*/) override { return nullptr; };
    HdBprim* CreateFallbackBprim(const TfToken& /*typeId*/) override { return nullptr; };

    void DestroyRprim(HdRprim* rPrim) override;
    void DestroySprim(HdSprim* sprim) override;
    void DestroyBprim(HdBprim* bprim) override {};

    // IMPORTANT: MaterialX Networks will NOT BE PROCESSED by Hydra unless you specify the context here.
    [[nodiscard]] TfTokenVector GetMaterialRenderContexts() const override { return { TfToken("mtlx") }; }

    void CommitResources(HdChangeTracker* pChangeTracker) override;

    [[nodiscard]] HdRenderParam* GetRenderParam() const override { return nullptr; };

    inline RenderContext* GetRenderContext() { return m_RenderContext; };
    inline std::mutex&    GetRenderContextMutex() { return m_RenderContextMutex; }

private:

    // Reference to the custom Vulkan driver implementation.
    RenderContext* m_RenderContext {};
    std::mutex     m_RenderContextMutex;

    HdResourceRegistrySharedPtr m_ResourceRegistry;
};

#endif
