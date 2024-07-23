#ifndef PCH_H
#define PCH_H

#include <volk.h>
#include <vk_mem_alloc.h>
#include <tiny_obj_loader.h>
#include <stb_image.h>
#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>

#include <fstream>
#include <intrin.h>

// LivePP Includes (If enabled)
// ---------------------------------------------------------

#ifdef USE_LIVEPP
    #include <LivePP/API/x64/LPP_API_x64_CPP.h>
#endif

// GLM Includes
// ---------------------------------------------------------

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

// OpenUSD Includes
// ---------------------------------------------------------

#include <pxr/pxr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/staticTokens.h>

// Hydra Core
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>

// HDX (Hydra Utilities)
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/taskController.h>

// USD Hydra Scene Delegate Implementation.
#include <pxr/usdImaging/usdImaging/delegate.h>

PXR_NAMESPACE_USING_DIRECTIVE

#endif