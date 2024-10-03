#include <Common.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <FreeCamera.h>

#define USE_FREE_CAMERA

// Local Utils
// ---------------------------------------------------------

std::atomic<bool> s_StageLoaded;

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

    spdlog::info("Parsing stage: {}", fileName);

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

    // Done.
    // ---------------------

    spdlog::info("Successfully parsed stage and populated scene delegate.", fileName);

    s_StageLoaded.store(true);
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

#ifdef _DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

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

    static int  s_DebugSceneIndex  = 0U;
    const char* kDebugScenePaths[] = { "..\\Assets\\scene.usd", "C:\\Development\\hercules\\cockpit.usd" };

    static int s_DebugModeIndex = 0U; // NOLINT

    std::jthread stageLoadingThread;

    auto RecordInterface = [&]()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.2F);

        if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::BeginDisabled(s_StageLoaded.load());

            ImGui::Text("Stage Path:");

            ImGui::SameLine();

            // ImGui::InputText("##", static_cast<char*>(s_USDPath), IM_ARRAYSIZE(s_USDPath));
            ImGui::Combo("##", &s_DebugSceneIndex, kDebugScenePaths, IM_ARRAYSIZE(kDebugScenePaths));

            ImGui::SameLine();

            if (ImGui::Button("Load"))
            {
                // Offload stage loading to a worker thread.
                stageLoadingThread = std::jthread([&]() { LoadStage(pRenderIndex, pSceneDelegate, pUsdStage, kDebugScenePaths[s_DebugSceneIndex]); });
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            const char* kModeNames[] = { "None", "MeshID", "PrimitiveID", "BarycentricCoordinate", "Depth", "Albedo" };
            ImGui::Combo("Debug", &s_DebugModeIndex, kModeNames, IM_ARRAYSIZE(kModeNames));

            ImGui::Separator();

            if (ImGui::BeginChild("LogSubWindow", ImVec2(600, 400), 1, ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::TextUnformatted(loggerMemory->str().c_str());

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0F);
            }
            ImGui::EndChild();

            ImGui::SetNextItemWidth(10U);

            // Display the FPS in the window
            ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0F);

            // Report VRAM
            {
                VmaTotalStatistics memoryStats;
                vmaCalculateStatistics(pRenderContext->GetAllocator(), &memoryStats);

                ImGui::SameLine();
                ImGui::Text("| VRAM: %f MB", static_cast<float>(memoryStats.total.statistics.allocationBytes) / (1024.0F * 1024.0F));
            }

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

        // Also forward the debug mode.
        pRenderDelegate->SetRenderSetting(kTokenDebugMode, VtValue(&s_DebugModeIndex));

#ifdef USE_FREE_CAMERA
        freeCamera.Update(static_cast<float>(frameParams.deltaTime));
#endif

        // Defer Hydra execution until a scene is loaded.
        if (!s_StageLoaded.load())
        {
            // TODO(parsa): Merge these barriers.

            VulkanColorImageBarrier(frameParams.cmd,
                                    frameParams.backBuffer,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_ACCESS_2_MEMORY_READ_BIT,
                                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT);

            VulkanColorImageBarrier(frameParams.cmd,
                                    frameParams.backBuffer,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                                    VK_ACCESS_2_MEMORY_READ_BIT,
                                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

            return;
        }

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
