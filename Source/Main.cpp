#include <RenderContext.h>
#include <RenderPass.h>
#include <RenderDelegate.h>
#include <Common.h>

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

    // Create render delegate.
    // ---------------------

    auto pRenderDelegate = std::make_unique<RenderDelegate>();
    TF_VERIFY(pRenderDelegate != nullptr);

    // Wrap the RenderContext into a USD Hydra Driver. 
    // -------------------------------------- 

    HdDriver renderContextHydraDriver(kTokenRenderContextDriver, VtValue(pRenderContext.get()));
    
    // Create render index from the delegate. 
    // ---------------------

    auto pRenderIndex = HdRenderIndex::New(pRenderDelegate.get(), { &renderContextHydraDriver });
    TF_VERIFY(pRenderIndex != nullptr);

    // Construct a scene delegate from the stock OpenUSD scene delegate implementation.
    // ---------------------

    auto pSceneDelegate = new UsdImagingDelegate(pRenderIndex, SdfPath::AbsoluteRootPath());
    TF_VERIFY(pSceneDelegate != nullptr);

    // Load a USD Stage.
    // ---------------------

    auto pUsdStage = pxr::UsdStage::Open("C:\\Development\\MarketScene\\MarketScene.usd");
    TF_VERIFY(pUsdStage != nullptr);

    // Pipe the USD stage into the scene delegate (will create render primitives in the render delegate).
    // ---------------------

    pSceneDelegate->Populate(pUsdStage->GetPseudoRoot());

    // Create the render tasks.
    // ---------------------

    HdxTaskController taskController(pRenderIndex, SdfPath("/taskController"));
    {
        auto params = HdxRenderTaskParams();
        {
            params.viewport = GfVec4i(0, 0, kWindowWidth, kWindowHeight);
        }

        // The "Task Controller" will automatically configure an HdxRenderTask
        // (which will create and invoke our delegate's renderpass).
        taskController.SetRenderParams(params);
        taskController.SetRenderViewport({ 0, 0, kWindowWidth, kWindowHeight });
    }

    // Initialize the Hydra engine. 
    // ---------------------

    HdEngine engine;

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](FrameParams frameParams)
    {
        // Forward the current backbuffer and commandbuffer to the delegate. 
        // There might be a simpler way to manage this by writing my own HdTask, but
        // it would require sacrificing the simplicity that HdxTaskController offers.
        pRenderDelegate->SetRenderSetting(kTokenCurrenFrameParams, VtValue(&frameParams));

        // Invoke Hydra!
        auto renderTasks = taskController.GetRenderingTasks();
        engine.Execute(pRenderIndex, &renderTasks);
    };
    
    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands);

    // Destroy LivePP Agent.
    // ------------------------------------------------
    
#ifdef USE_LIVEPP
    lpp::LppDestroyDefaultAgent(&lppAgent);
#endif
}