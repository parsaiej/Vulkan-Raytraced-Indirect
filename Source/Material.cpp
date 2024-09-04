#include <Common.h>
#include <Material.h>
#include <RenderDelegate.h>
#include <ResourceRegistry.h>
#include <RenderContext.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>

#include <MaterialXGenGlsl/VkResourceBindingContext.h>
#include <MaterialXGenGlsl/VkShaderGenerator.h>
#include <MaterialXGenShader/Shader.h>

// #define MATERIAL_DEBUG_PRINT_NETWORK

HdDirtyBits Material::GetInitialDirtyBitsMask() const { return HdChangeTracker::AllSceneDirtyBits; }

void TraceNodeRecursive(HdMaterialNetwork2* pNetwork, HdMaterialNode2* pNode, uint32_t traceDepth = 0U)
{
    spdlog::info("{}NODE: {}", std::string(traceDepth, '\t'), pNode->nodeTypeId.GetText());

    for (const auto& input : pNode->inputConnections)
    {
        spdlog::info("{}INPUT: {}", std::string(traceDepth + 1U, '\t'), input.first.GetText());

        assert(input.second.size() <= 1);

        for (const auto& connection : input.second)
        {
            auto node = pNetwork->nodes.find(connection.upstreamNode);

            // Follow the connection to the next node.
            TraceNodeRecursive(pNetwork, &node->second, traceDepth + 1U);
        }
    }

    for (const auto& parameter : pNode->parameters)
    {
        if (parameter.second.IsHolding<SdfAssetPath>())
            spdlog::info("{}ASSET: {}", std::string(traceDepth + 1U, '\t'), parameter.second.Get<SdfAssetPath>().GetResolvedPath());
    }
}

template <typename T>
T TryGetSingleParameterForInput(const char* inputName, HdMaterialNetwork2* pNetwork, HdMaterialNode2* pNode)
{
    for (const auto& input : pNode->inputConnections)
    {
        if (strcmp(input.first.GetText(), inputName) != 0)
            continue;

        for (const auto& connection : input.second)
        {
            auto node = pNetwork->nodes.find(connection.upstreamNode);

            // Follow the connection to the next node.
            return TryGetSingleParameterForInput<T>(inputName, pNetwork, &node->second);
        }
    }

    for (const auto& parameter : pNode->parameters)
    {
        if (parameter.second.IsHolding<T>())
            return parameter.second.Get<T>();
    }

    return T();
}

#if 0

void ReconstructMaterialXDocument(HdMaterialNetwork2* pNetwork, const SdfPath& rootNodePath, const HdMaterialNode2& rootNode, const SdfPath& materialID)
{
    // Reconstruct the MaterialX Document from the HdMaterialNetwork
    HdMtlxTexturePrimvarData mxTextureData;
    auto pDocument = HdMtlxCreateMtlxDocumentFromHdNetwork(*pNetwork, rootNode, rootNodePath, materialID, HdMtlxStdLibraries(), &mxTextureData);

    // Validate the document.
    Check(pDocument->validate(), "Failed to validate the MaterialX document.");

#if 0
    // Write to disk for debug purposes. 
    MaterialX::writeToXmlFile(pDocument, std::format("{}.mtlx", rootNodePath.GetName()));
#endif

    // Create shader generator.
    MaterialX::GenContext generationContext(MaterialX::VkShaderGenerator::create());

    // Hardcode path to the materialx std libraries.
    // NOTE: Should be able to remove this simply by moving /libraries/ relative
    // to executable.
    generationContext.registerSourceCodeSearchPath(R"(C:\Development\OpenUSD-Install\)");

    // Find renderable elements in the Mtlx Document.
    auto renderableElements = MaterialX::findRenderableElements(pDocument);

    // Extract the generated shader.
    auto pShader = generationContext.getShaderGenerator().generate("Shader", renderableElements[0], generationContext);
}

#endif

void Material::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits)
{
    if ((*pDirtyBits & HdChangeTracker::AllSceneDirtyBits) == 0U)
        return;

    PROFILE_START("Sync Material");

    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());

    auto id = GetId();

    auto materialResource = pSceneDelegate->GetMaterialResource(id);

    if (!materialResource.IsHolding<HdMaterialNetworkMap>())
        return;

    // Convert to the newer network model.
    auto network = HdConvertToHdMaterialNetwork2(materialResource.UncheckedGet<HdMaterialNetworkMap>());

    // Get the Surface Terminal
    const auto& terminalConnectionIterator = network.terminals.find(HdMaterialTerminalTokens->surface);

    Check(terminalConnectionIterator != network.terminals.end(), "Failed to locate a surface node on the material.");

    // Grab the terminal connection.
    auto surfaceTerminalConnection = terminalConnectionIterator->second;

    // Grab the root surface node.
    auto rootNode = network.nodes.find(surfaceTerminalConnection.upstreamNode);

#ifdef MATERIAL_DEBUG_PRINT_NETWORK
    // Debug print the surface material graph.
    TraceNodeRecursive(&network, &rootNode->second);
#endif

    // Obtain the resource registry + push the material request.
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(pSceneDelegate->GetRenderIndex().GetResourceRegistry());
    {
        m_ResourceHandle = pResourceRegistry->PushMaterialRequest(
            { id,
              TryGetSingleParameterForInput<SdfAssetPath>(kMaterialInputBaseColor, &network, &rootNode->second),
              TryGetSingleParameterForInput<SdfAssetPath>(kMaterialInputNormal, &network, &rootNode->second),
              TryGetSingleParameterForInput<SdfAssetPath>(kMaterialInputRoughness, &network, &rootNode->second),
              TryGetSingleParameterForInput<SdfAssetPath>(kMaterialInputMetallic, &network, &rootNode->second) });
    }

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;

    PROFILE_END;
}
