#ifndef PRECOMPILED_H
#define PRECOMPILED_H

#ifdef _DEBUG
// Logging of VMA memory leaks in debug mode.
#define VMA_LEAK_LOG_FORMAT(format, ...) \
    do                                   \
    {                                    \
        printf((format), __VA_ARGS__);   \
        printf("\n");                    \
    }                                    \
    while (false)
#endif

/* clang-format off */
// NOTE: Preserve include order here (formatter wants to sort alphabetically.)
#include <volk.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
/* clang-format on */

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h> // For imgui.

#include <stb_image.h>
#include <tiny_obj_loader.h>

#include <fstream>
#include <intrin.h>
#include <filesystem>
#include <queue>

// Superluminal Includes (If enabled)
// ---------------------------------------------------------

#ifdef USE_SUPERLUMINAL
#include <Superluminal/PerformanceAPI.h>
#endif

// LivePP Includes (If enabled)
// ---------------------------------------------------------

#ifdef USE_LIVEPP
#include <LivePP/API/x64/LPP_API_x64_CPP.h>
#endif

// Imgui Includes
// ---------------------------------------------------------

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

// DDS Loading Util
// ---------------------------------------------------------

#include <dds.hpp>

// GLM Includes
// ---------------------------------------------------------

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

// DirectX Tool Kit is used for Free Camera.
// ---------------------------------------------------------

#include <directxtk12/Keyboard.h>
#include <directxtk12/Mouse.h>

#include <DirectXMath.h>
#include <SimpleMath.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;

// meshoptimizer
// ---------------------------------------------------------

#include <meshoptimizer.h>

// TBB Includes
// ---------------------------------------------------------

#include <tbb/parallel_for_each.h>
#include <tbb/task_group.h>

// OpenUSD Includes
// ---------------------------------------------------------

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>

// Hydra Core
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/imaging/hd/instancer.h>

// MaterialX Support.
#include <pxr/imaging/hdMtlx/hdMtlx.h>

// HDX (Hydra Utilities)
#include <pxr/imaging/hdx/freeCameraSceneDelegate.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/taskController.h>

// USD Hydra Scene Delegate Implementation.
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

PXR_NAMESPACE_USING_DIRECTIVE

// FidelityFX
// ---------------------------------------------------------

#include <FidelityFX/host/ffx_brixelizer.h>

#endif
