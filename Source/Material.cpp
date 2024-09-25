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

#include <cstddef>

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

class ImageLoader
{
public:

    explicit ImageLoader(const SdfAssetPath& imagePath) : m_Format(VK_FORMAT_UNDEFINED)
    {
        if (std::filesystem::path(imagePath.GetResolvedPath()).extension().string() == ".dds")
        {
            Check(dds::readFile(imagePath.GetResolvedPath(), &m_DDSImage) == 0U, "Failed to load DDS image to memory.");

            // Read out the image data.
            m_Width  = static_cast<int>(m_DDSImage.width);
            m_Height = static_cast<int>(m_DDSImage.height);

            m_BytesPerPixel = dds::getBitsPerPixel(m_DDSImage.format) >> 3U;

            // Extract pointer to image data.
            m_Data = m_DDSImage.mipmaps.front().data();

            // Extract the format.
            m_Format = dds::getVulkanFormat(m_DDSImage.format, m_DDSImage.supportsAlpha);

            // We did not load with STB.
            m_IsSTB = false;
        }
        else
        {
            int channels = 0;
            m_Data       = stbi_load(imagePath.GetResolvedPath().c_str(), &m_Width, &m_Height, &channels, 0U);

            if (channels != 4U)
                InterleaveImageAlpha(reinterpret_cast<stbi_uc**>(&m_Data), m_Width, m_Height, channels);

            // Hardcode for now...
            m_Format = VK_FORMAT_R8G8B8A8_SRGB;

            // The hardcoded format is 4-bytes per pixel.
            m_BytesPerPixel = 4U;

            // Need to make sure we free the memory in case of STB.
            m_IsSTB = true;
        }
    }

    ~ImageLoader()
    {
        if (m_Data != nullptr && m_IsSTB)
            stbi_image_free(m_Data);
    }

    [[nodiscard]] inline const void*     GetData() const { return m_Data; }
    [[nodiscard]] inline GfVec2i         GetDim() const { return { m_Width, m_Height }; }
    [[nodiscard]] inline const VkFormat& GetFormat() const { return m_Format; }
    [[nodiscard]] inline const uint32_t& GetStride() const { return m_BytesPerPixel; }

private:

    VkFormat   m_Format {};
    uint32_t   m_BytesPerPixel {};
    void*      m_Data {};
    int        m_Width {};
    int        m_Height {};
    bool       m_IsSTB {};
    dds::Image m_DDSImage {};
};

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

    // Load images.
    ImageLoader albedo(TryGetSingleParameterForInput<SdfAssetPath>(kMaterialInputBaseColor, &network, &rootNode->second));

    // Make a request to the image pool.
    MaterialRequest request { this };
    {
        request.albedo = { nullptr, albedo.GetStride(), albedo.GetDim(), albedo.GetFormat() };
    }
    pResourceRegistry->PushMaterialRequest(request);

    // Copy into the mapped pointers.
    if (albedo.GetFormat() != VK_FORMAT_UNDEFINED)
        memcpy(request.albedo.data, albedo.GetData(), static_cast<size_t>(albedo.GetStride() * albedo.GetDim()[0]) * albedo.GetDim()[1]);

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;

    PROFILE_END;
}
