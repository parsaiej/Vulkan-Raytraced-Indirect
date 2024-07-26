#include <RenderDelegate.h>
#include <RenderPass.h>
#include <RenderContext.h>
#include <ResourceRegistry.h>
#include <Scene.h>
#include <Mesh.h>
#include <Common.h>
#include <Camera.h>

// Render Delegate Implementation
// ------------------------------------------------------------

void RenderDelegate::SetDrivers(HdDriverVector const& drivers)
{
    for (const auto& driver : drivers)
    {
        if (driver->name == kTokenRenderContextDriver)
            m_RenderContext = driver->driver.UncheckedGet<RenderContext*>();
    }

    Check(m_RenderContext, "Failed to find the custom Vulkan driver for Hydra.");
    
    m_ResourceRegistry = std::make_shared<ResourceRegistry>(m_RenderContext);
}

HdRenderPassSharedPtr RenderDelegate::CreateRenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr(new RenderPass(pRenderIndex, collection, this));  
}

HdRprim* RenderDelegate::CreateRprim(TfToken const& typeId, SdfPath const& rprimId)
{
    if (typeId != HdPrimTypeTokens->mesh)
    {
        spdlog::warn("Skipping non-mesh Hydra Rprim.");
        return nullptr;
    }

    return new Mesh(rprimId, this);
}

HdSprim* RenderDelegate::CreateSprim(TfToken const& typeId, SdfPath const& sprimId)
{
    if (typeId != HdPrimTypeTokens->camera)
    {
        spdlog::warn("Skipping non-camera Hydra Sprim.");
        return nullptr;
    }

    return new HdCamera(sprimId);
}

void RenderDelegate::CommitResources(HdChangeTracker* pChangeTracker)
{
    // Upload resources to GPU. 
    m_ResourceRegistry->Commit();
}