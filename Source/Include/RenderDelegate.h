#ifndef RENDER_DELEGATE_H
#define RENDER_DELEGATE_H

class RenderContext;
class ResourceRegistry;

// USD Hydra Render Delegate
// ---------------------------------------------------------

const TfTokenVector kSupportedRPrimTypes = {
    HdPrimTypeTokens->mesh,
};

const TfTokenVector kSupportedSPrimTypes = { HdPrimTypeTokens->camera, HdPrimTypeTokens->material };

const TfTokenVector kSupportedBPrimTypes = {};

// This token is used for identifying our custom Hydra driver.
const TfToken kTokenRenderContextDriver = TfToken("RenderContextDriver");
const TfToken kTokenCurrenFrameParams   = TfToken("CurrentFrameParams");

class RenderDelegate : public HdRenderDelegate
{
public:
    RenderDelegate() {};
    RenderDelegate(HdRenderSettingsMap const& settingsMap) {};
    virtual ~RenderDelegate() {};

    void SetDrivers(HdDriverVector const& drivers) override;

    const TfTokenVector& GetSupportedRprimTypes() const override { return kSupportedRPrimTypes; };
    const TfTokenVector& GetSupportedSprimTypes() const override { return kSupportedSPrimTypes; };
    const TfTokenVector& GetSupportedBprimTypes() const override { return kSupportedBPrimTypes; };

    HdResourceRegistrySharedPtr GetResourceRegistry() const override { return m_ResourceRegistry; };

    HdRenderPassSharedPtr CreateRenderPass(
        HdRenderIndex* index, HdRprimCollection const& collection) override;

    HdInstancer* CreateInstancer(HdSceneDelegate* delegate, SdfPath const& id) override
    {
        return nullptr;
    };
    void DestroyInstancer(HdInstancer* instancer) override {};

    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId) override;
    HdSprim* CreateSprim(TfToken const& typeId, SdfPath const& sprimId) override;
    HdBprim* CreateBprim(TfToken const& typeId, SdfPath const& bprimId) override
    {
        return nullptr;
    };

    HdSprim* CreateFallbackSprim(TfToken const& typeId) override { return nullptr; };
    HdBprim* CreateFallbackBprim(TfToken const& typeId) override { return nullptr; };

    void DestroyRprim(HdRprim* rPrim) override {};
    void DestroySprim(HdSprim* sprim) override {};
    void DestroyBprim(HdBprim* bprim) override {};

    TfTokenVector GetMaterialRenderContexts() const override { return { TfToken("mtlx") }; }

    void CommitResources(HdChangeTracker* pTracker) override;

    HdRenderParam* GetRenderParam() const override { return nullptr; };

    inline RenderContext* GetRenderContext() { return m_RenderContext; };
    inline std::mutex& GetRenderContextMutex() { return m_RenderContextMutex; }

private:
    // Reference to the custom Vulkan driver implementation.
    RenderContext* m_RenderContext;
    std::mutex m_RenderContextMutex;

    HdResourceRegistrySharedPtr m_ResourceRegistry;
};

#endif