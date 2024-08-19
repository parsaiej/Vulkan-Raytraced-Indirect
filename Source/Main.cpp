#include <Common.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <FreeCamera.h>

#define USE_FREE_CAMERA

// Local Utils
// ---------------------------------------------------------

// Utility for loading / reloading USD stages.
void LoadStage(HdRenderIndex* pRenderIndex, std::unique_ptr<UsdImagingDelegate>& pSceneDelegate, UsdStageRefPtr pUsdStage, const char* fileName)
{
    // First make sure the stage exists on disk.
    // ---------------------

    if (!std::filesystem::exists(std::filesystem::path(fileName)))
    {
        spdlog::error("The provided file path does not exist.");
        return;
    }

    // Load a USD Stage.
    // ---------------------

    PROFILE_START("Load USD Stage");

    pUsdStage = pxr::UsdStage::Open(fileName);
    TF_VERIFY(pUsdStage != nullptr);

    PROFILE_END;

    // (Re)-create the scene delegate.
    // ---------------------

    pSceneDelegate = std::make_unique<UsdImagingDelegate>(pRenderIndex, SdfPath::AbsoluteRootPath());
    TF_VERIFY(pSceneDelegate != nullptr);

    // Pipe the USD stage into the scene delegate (will create render primitives
    // in the render delegate).
    // ---------------------

    PROFILE_START("Populate Hydra Scene Delegate.");

    pSceneDelegate->Populate(pUsdStage->GetPseudoRoot());

    PROFILE_END;
}

// Executable implementation.
// ---------------------------------------------------------

int main()
{
    // Configure logging.
    // --------------------------------------

    auto loggerMemory = std::make_shared<std::stringstream>();
    auto loggerSink   = std::make_shared<spdlog::sinks::ostream_sink_mt>(*loggerMemory);
    auto logger       = std::make_shared<spdlog::logger>("", loggerSink);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^[%l] %v%$");

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

    // Create a free camera.
    // ---------------------

    FreeCamera freeCamera(pRenderIndex, SdfPath("/freeCamera"), pRenderContext->GetWindow());

    // Empty pointer to a USD stage.
    // ---------------------

    UsdStageRefPtr pUsdStage;

    // Empty pointer to USD scene delegate.
    // ---------------------

    std::unique_ptr<UsdImagingDelegate> pSceneDelegate;

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

    static char s_USDPath[1024U] = "..\\Assets\\scene.usd"; // NOLINT

    auto RecordInterface = [&]()
    {
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0));

        if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::Text("Stage Path:");

            ImGui::SameLine();

            ImGui::InputText("##", static_cast<char*>(s_USDPath), IM_ARRAYSIZE(s_USDPath));

            ImGui::SameLine();

            if (ImGui::Button("Load"))
                LoadStage(pRenderIndex, pSceneDelegate, pUsdStage, static_cast<char*>(s_USDPath));

            ImGui::Separator();

            if (ImGui::BeginChild("LogSubWindow", ImVec2(0, 300), 1, ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::TextUnformatted(loggerMemory->str().c_str());

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0F);
            }
            ImGui::EndChild();

            // Display the FPS in the window
            ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0F);

            ImGui::End();
        }
    };

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
