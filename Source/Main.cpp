#include <Common.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>

// #define USE_FREE_CAMERA

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
    lppAgent.EnableModule(lpp::LppGetCurrentModulePath(),
        lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);
#endif

    // Launch Vulkan + OS Window
    // --------------------------------------

    std::unique_ptr<RenderContext> pRenderContext =
        std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);

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

    // Load a USD Stage.
    // ---------------------

    auto pUsdStage = pxr::UsdStage::Open("..\\Assets\\scene.usd");
    TF_VERIFY(pUsdStage != nullptr);

#ifndef USE_HYDRA_SCENE_INDEX
    // Construct a scene delegate from the stock OpenUSD scene delegate
    // implementation.
    // ---------------------
    auto pSceneDelegate =
        std::make_unique<UsdImagingDelegate>(pRenderIndex, SdfPath::AbsoluteRootPath());
    TF_VERIFY(pSceneDelegate != nullptr);

    // Pipe the USD stage into the scene delegate (will create render primitives
    // in the render delegate).
    // ---------------------
    pSceneDelegate->Populate(pUsdStage->GetPseudoRoot());
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

    auto pFreeCameraSceneDelegate =
        std::make_unique<HdxFreeCameraSceneDelegate>(pRenderIndex, SdfPath("/freeCamera"));

    // Create the render tasks.
    // ---------------------

    HdxTaskController taskController(pRenderIndex, SdfPath("/taskController"));
    {
        // The "Task Controller" will automatically configure an HdxRenderTask
        // (which will create and invoke our delegate's renderpass).
        taskController.SetRenderViewport({ 0, 0, kWindowWidth, kWindowHeight });

#ifdef USE_FREE_CAMERA
        taskController.SetCameraPath(pFreeCameraSceneDelegate->GetCameraId());
#else
        taskController.SetCameraPath(SdfPath("/cameras/camera1"));
#endif
    }

    // Initialize the Hydra engine.
    // ---------------------

    HdEngine engine;

    // Command Recording
    // ------------------------------------------------

    float time = 0.0f;

    auto RecordCommands = [&](FrameParams frameParams) {
        // Forward the current backbuffer and commandbuffer to the delegate.
        // There might be a simpler way to manage this by writing my own HdTask, but
        // it would require sacrificing the simplicity that HdxTaskController
        // offers.
        pRenderDelegate->SetRenderSetting(kTokenCurrenFrameParams, VtValue(&frameParams));

#ifdef USE_FREE_CAMERA
        auto WrapMatrix = [](glm::mat4 m) {
            return GfMatrix4f(m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2],
                m[1][3], m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3]);
        };

        // Define the camera position (eye), target position (center), and up vector
        glm::vec3 eye    = glm::vec3(0.5f * sin(time), 0.5f, 0.5f * cos(time));
        glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 up     = glm::vec3(0.0f, 1.0f, 0.0f);

        // Create the view matrix using glm::lookAt
        glm::mat4 view = glm::lookAt(eye, center, up);
        glm::mat4 proj = glm::perspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);

        // Manually use GLM since USD's matrix utilities are very bad.
        // GfMatrix4f::LookAt seems super broken.
        pFreeCameraSceneDelegate->SetMatrices(
            (GfMatrix4d)WrapMatrix(view), (GfMatrix4d)WrapMatrix(proj));
#endif

        // Invoke Hydra
        auto renderTasks = taskController.GetRenderingTasks();
        engine.Execute(pRenderIndex, &renderTasks);

        time += (float)frameParams.deltaTime;
    };

    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands);

    // Progarm is exiting, free GPU memory.
    // ------------------------------------------------

    pRenderDelegate->GetResourceRegistry()->GarbageCollect();

    // Destroy LivePP Agent.
    // ------------------------------------------------

#ifdef USE_LIVEPP
    lpp::LppDestroyDefaultAgent(&lppAgent);
#endif
}