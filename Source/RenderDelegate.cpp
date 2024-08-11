#include <Common.h>
#include <Material.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Scene.h>

// Render Delegate Implementation
// ------------------------------------------------------------

void RenderDelegate::SetDrivers(HdDriverVector const& drivers)
{
    for (const auto& driver : drivers)
    {
        if (driver->name == kTokenRenderContextDriver)
            m_RenderContext = driver->driver.UncheckedGet<RenderContext*>();
    }

    Check(m_RenderContext != nullptr, "Failed to find the custom Vulkan driver for Hydra.");

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
    if (typeId == HdPrimTypeTokens->camera)
        return new HdCamera(sprimId);

    if (typeId == HdPrimTypeTokens->material)
        return new Material(sprimId, this);

    return nullptr;
}

void RenderDelegate::CommitResources(HdChangeTracker* pChangeTracker)
{
    // Upload resources to GPU.
    m_ResourceRegistry->Commit();
}