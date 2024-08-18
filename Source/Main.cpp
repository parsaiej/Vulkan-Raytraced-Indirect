#include <Common.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <FreeCamera.h>

#define USE_FREE_CAMERA

// Hydra 2.0 replaced Scene Delegates with Scene Index concept:
// https://openusd.org/release/api/_page__hydra__getting__started__guide.html
// #define USE_HYDRA_SCENE_INDEX

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

    PROFILE_START("Initialize Render Context");

    std::unique_ptr<RenderContext> pRenderContext = std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);

    PROFILE_END;

    // Create render delegate.
    // ---------------------

    auto pRenderDelegate = std::make_unique<RenderDelegate>();
    TF_VERIFY(pRenderDelegate != nullptr);

    // Wrap the RenderContext into a USD Hydra Driver.
    // --------------------------------------

    HdDriver renderContextHydraDriver(kTokenRenderContextDriver, VtValue(pRenderContext.get()));

    // Create render index from the delegate.
    // ---------------------

    auto* pRenderIndex = HdRenderIndex::New(pRenderDelegate.get(), { &renderContextHydraDriver });
    TF_VERIFY(pRenderIndex != nullptr);

    // Load a USD Stage.
    // ---------------------

    PROFILE_START("Load USD Stage");

    auto pUsdStage = pxr::UsdStage::Open("..\\Assets\\scene.usd");
    TF_VERIFY(pUsdStage != nullptr);

    PROFILE_END;

#ifndef USE_HYDRA_SCENE_INDEX
    // Construct a scene delegate from the stock OpenUSD scene delegate
    // implementation.
    // ---------------------
    auto pSceneDelegate = std::make_unique<UsdImagingDelegate>(pRenderIndex, SdfPath::AbsoluteRootPath());
    TF_VERIFY(pSceneDelegate != nullptr);

    // Pipe the USD stage into the scene delegate (will create render primitives
    // in the render delegate).
    // ---------------------

    PROFILE_START("Populate Hydra Scene Delegate.");

    pSceneDelegate->Populate(pUsdStage->GetPseudoRoot());

    PROFILE_END;
#else
    // Construct a scene index from the stock OpenUSD scene index implementation.
    // ---------------------
    auto pSceneIndex = UsdImagingStageSceneIndex::New(nullptr);

    pSceneIndex->SetStage(pUsdStage);

    // Insert the scene index into the render index.
    pRenderIndex->InsertSceneIndex(pSceneIndex, SdfPath(), false);
#endif

    // Create a free camera.
    // ---------------------

    FreeCamera freeCamera(pRenderIndex, SdfPath("/freeCamera"), pRenderContext->GetWindow());

    // Create the render tasks.
    // ---------------------

    HdxTaskController taskController(pRenderIndex, SdfPath("/taskController"));
    {
        // The "Task Controller" will automatically configure an HdxRenderTask
        // (which will create and invoke our delegate's renderpass).
        taskController.SetRenderViewport({ 0, 0, kWindowWidth, kWindowHeight });

#ifdef USE_FREE_CAMERA
        taskController.SetCameraPath(freeCamera.GetCameraId());
#else
        taskController.SetCameraPath(SdfPath("/cameras/camera1"));
#endif
    }

    // Initialize the Hydra engine.
    // ---------------------

    HdEngine engine;

    // UI
    // ------------------------------------------------

    auto RecordInterface = [&]() {};

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](FrameParams frameParams)
    {
        // Forward the current backbuffer and commandbuffer to the delegate.
        // There might be a simpler way to manage this by writing my own HdTask, but
        // it would require sacrificing the simplicity that HdxTaskController
        // offers.
        pRenderDelegate->SetRenderSetting(kTokenCurrenFrameParams, VtValue(&frameParams));

#ifdef USE_FREE_CAMERA
        freeCamera.Update(static_cast<float>(frameParams.deltaTime));
#endif

        // Invoke Hydra
        auto renderTasks = taskController.GetRenderingTasks();
        engine.Execute(pRenderIndex, &renderTasks);
    };

    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands, RecordInterface);

    // Progarm is exiting, free GPU memory.
    // ------------------------------------------------

    PROFILE_START("Release Resources");

    pRenderDelegate->GetResourceRegistry()->GarbageCollect();

    PROFILE_END;

    // Destroy LivePP Agent.
    // ------------------------------------------------

#ifdef USE_LIVEPP
    lpp::LppDestroyDefaultAgent(&lppAgent);
#endif
}
