#include <Material.h>
#include <Common.h>

HdDirtyBits Material::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void Material::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits)
{
    if (!(*pDirtyBits & HdChangeTracker::AllSceneDirtyBits))
        return;

    auto id = GetId();

    auto materialResource = pSceneDelegate->GetMaterialResource(id);
    
    if (!materialResource.IsHolding<HdMaterialNetworkMap>())
        return;

    // Convert to the newer network model.
    auto network = HdConvertToHdMaterialNetwork2(materialResource.UncheckedGet<HdMaterialNetworkMap>());

    // Get Standard Libraries and SearchPaths (for mxDoc and mxShaderGen)
    const MaterialX::DocumentPtr&    mxStandardLibraries = HdMtlxStdLibraries();
    const MaterialX::FileSearchPath& mxSearchPaths       = HdMtlxSearchPaths();

    std::pair<SdfPath, HdMaterialNode2*> surfaceNode;
    {
        // Get the Surface or Volume Terminal
        auto const& terminalConnectionIterator = network.terminals.find(HdMaterialTerminalTokens->surface);

        Check(terminalConnectionIterator != network.terminals.end(), "Failed to locate a surface node on the material.");

        // Grab the terminal connection.
        auto surfaceTerminalConnection = terminalConnectionIterator->second;

        // Cache surface node path.
        surfaceNode.first = surfaceTerminalConnection.upstreamNode;

        auto const& terminalNodeIterator = network.nodes.find(surfaceNode.first);
        
        // Found the surface node.
        surfaceNode.second = &terminalNodeIterator->second;
    }

    Check(surfaceNode.second != nullptr, "Failed to locate a surface node on the material.");

    // Create the MaterialX Document from the HdMaterialNetwork
    HdMtlxTexturePrimvarData mxTextureData;
    auto pDocument = HdMtlxCreateMtlxDocumentFromHdNetwork
    (
        network, 
        *surfaceNode.second, 
        surfaceNode.first, 
        id,
        mxStandardLibraries, 
        &mxTextureData
    );

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}