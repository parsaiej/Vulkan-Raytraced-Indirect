#include <RenderDelegate.h>
#include <RenderPass.h>
#include <Mesh.h>
#include <Common.h>

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
}

HdRenderPassSharedPtr RenderDelegate::CreateRenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection)
{
    spdlog::info("Registering Hydra Renderpass.");
    return HdRenderPassSharedPtr(new RenderPass(pRenderIndex, collection, this));  
}

HdRprim* RenderDelegate::CreateRprim(TfToken const& typeId, SdfPath const& rprimId)
{
    if (typeId != HdPrimTypeTokens->mesh)
    {
        spdlog::warn("Skipping non-mesh Hydra Rprim.");
        return nullptr;
    }
    
    spdlog::info("Parsing Hydra Mesh Rprim.");

    return new Mesh(rprimId, this);
}

HdSprim* RenderDelegate::CreateSprim(TfToken const& typeId, SdfPath const& rprimId)
{
    if (typeId != HdPrimTypeTokens->camera)
    {
        spdlog::warn("Skipping non-camera Hydra Sprim.");
        return nullptr;
    }
    
    spdlog::info("Parsing Hydra Camera Sprim.");

    return nullptr;
}