#include <RenderContext.h>
#include <RenderPass.h>
#include <RenderDelegate.h>
#include <Common.h>

bool ParseStage(const char* filePath, tinyusdz::Stage& stage)
{
    std::string wrn, err;
    bool parseResult = tinyusdz::LoadUSDFromFile(filePath, &stage, &wrn, &err);

    if (!wrn.empty())
        spdlog::warn("USD Parse Warning: {}", wrn);

    if (!err.empty())
        spdlog::error("USD Parse Error: {}", err);

    return parseResult;
}

// Executable implementation.
// ---------------------------------------------------------

int main()
{

#ifdef USE_LIVEPP
    // Locate the LivePP Agent. 
    auto lppAgent = lpp::LppCreateDefaultAgent(nullptr, L"..\\External\\LivePP");

    // Confirm LivePP instance is valid.
    Check(lpp::LppIsValidDefaultAgent(&lppAgent), "Failed to initialize LivePP");

    // Enable all loaded modules. 
    lppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);
#endif

    // Launch Vulkan + OS Window
    // --------------------------------------

    std::unique_ptr<RenderContext> pRenderContext = std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);

    // Load USD Stage
    // ------------------------------------------------
    
    tinyusdz::Stage stage;
    Check(ParseStage("..\\Assets\\scene.usd", stage), "Failed to parse the provided USD scene.");

    // Convert the stage into render-friendly primitives
    // ------------------------------------------------
    
    tinyusdz::tydra::RenderScene             renderScene;
    tinyusdz::tydra::RenderSceneConverter    renderSceneConverter;
    tinyusdz::tydra::RenderSceneConverterEnv renderSceneConverterEnv(stage);

    // Configure render scene before loading.
    renderSceneConverterEnv.set_search_paths( { "C:\\Development\\Vulkan-RayTraced-Indirect\\Assets\\" } );
    renderSceneConverterEnv.timecode = 0.0;

    if (!renderSceneConverter.ConvertToRenderScene(renderSceneConverterEnv, &renderScene))
    {
        spdlog::error("Render Scene Create Error: {}", renderSceneConverter.GetError());
        return 1;
    }
    else if (!renderSceneConverter.GetWarning().empty())
        spdlog::warn("Render Scene Create Warning: {}", renderSceneConverter.GetWarning());

    // Command Recording
    // ------------------------------------------------

    renderScene.meshes[0].

    // auto RecordCommands = [&](FrameParams frameParams)
    // {
    //     spdlog::info("Recording commands..."); 
    // };
    
    // Kick off render-loop.
    // ------------------------------------------------

    // pRenderContext->Dispatch(RecordCommands);

    // Destroy LivePP Agent.
    // ------------------------------------------------
    
#ifdef USE_LIVEPP
    lpp::LppDestroyDefaultAgent(&lppAgent);
#endif
}