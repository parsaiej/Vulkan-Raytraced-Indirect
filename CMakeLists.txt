cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/External/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

set(PROJECT_NAME Vulkan-RayTraced-Indirect)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
# --------------------------------

option(USE_SUPERLUMINAL "" ON)
option(USE_VK_LABELS "" ON)

# Check for the USD Installation Environment variable
# --------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (NOT DEFINED ENV{USD_INSTALL_DEBUG})
        message(FATAL_ERROR "\nDebug USD Installation not found. Please install USD and reference it with environment variable: USD_INSTALL_DEBUG")
    else()
        set(pxr_DIR $ENV{USD_INSTALL_DEBUG})
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    if (NOT DEFINED ENV{USD_INSTALL_RELEASE})
        message(FATAL_ERROR "\nRelease USD Installation not found. Please install USD and reference it with environment variable: USD_INSTALL_RELEASE")
    else()
        set(pxr_DIR $ENV{USD_INSTALL_RELEASE})
    endif()
endif()

# Configure Superluminal Performance
# --------------------------------

if (${USE_SUPERLUMINAL})
    if (NOT DEFINED ENV{SUPERLUMINAL_API_DIR})
        message(FATAL_ERROR "\nSuperluminal option was enabled but environment variable SUPERLUMINAL_API_DIR was not set.")
    endif()

    set(SuperluminalAPI_DIR $ENV{SUPERLUMINAL_API_DIR})
endif()

# Configure Superluminal Performance
# --------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(FFX_BACKEND_LIB    ${CMAKE_SOURCE_DIR}/External/FidelityFX/bin/debug/ffx_backend_vk_x64d.lib)
    set(FFX_BRIXELIZER_LIB ${CMAKE_SOURCE_DIR}/External/FidelityFX/bin/debug/ffx_brixelizer_x64d.lib)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(FFX_BACKEND_LIB    ${CMAKE_SOURCE_DIR}/External/FidelityFX/bin/release/ffx_backend_vk_x64.lib)
    set(FFX_BRIXELIZER_LIB ${CMAKE_SOURCE_DIR}/External/FidelityFX/bin/release/ffx_brixelizer_x64.lib)
endif()

# Packages
# --------------------------------

find_package(spdlog                REQUIRED)
find_package(Stb                   REQUIRED)
find_package(volk                  REQUIRED)
find_package(glfw3                 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(tinyobjloader         REQUIRED)
find_package(glm                   REQUIRED)
find_package(OpenGL                REQUIRED)
find_package(pxr                   REQUIRED)
find_package(directxtk12           REQUIRED)

if (${USE_SUPERLUMINAL})
    # Warning: Superluminal ships with file named FindSuperluminalAPI.cmake, it needs to be renamed to SuperluminalAPIConfig.cmake.
    find_package(SuperluminalAPI REQUIRED)
endif()

# Meshoptimizer
# --------------------------------

add_subdirectory(External/meshoptimizer/)

# Imgui
# --------------------------------

set(IMGUI_INC ${CMAKE_SOURCE_DIR}/External/imgui/
              ${CMAKE_SOURCE_DIR}/External/imgui/backends/)

file(GLOB IMGUI_SRC ${CMAKE_SOURCE_DIR}/External/imgui/imgui.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_widgets.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_tables.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_draw.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/imgui_demo.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/backends/imgui_impl_glfw.cpp
                    ${CMAKE_SOURCE_DIR}/External/imgui/backends/imgui_impl_vulkan.cpp)

# Executable
# --------------------------------

add_executable(${PROJECT_NAME} 
    Source/Main.cpp
    Source/Precompiled.cpp
    Source/RenderContext.cpp
    Source/RenderDelegate.cpp
    Source/RenderPass.cpp
    Source/ResourceRegistry.cpp
    Source/Mesh.cpp
    Source/Common.cpp
    Source/Material.cpp
    Source/FreeCamera.cpp
    ${IMGUI_SRC}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE Source/Include/Precompiled.h)

# Include
# --------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE
    Source/Include
    External/dds_image/include
    ${Stb_INCLUDE_DIR}
    ${PXR_INCLUDE_DIR}
    ${IMGUI_INC}
    External/FidelityFX/include
)

# Link
# --------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE 
    volk::volk_headers 
    spdlog::spdlog_header_only
    glfw
    GPUOpen::VulkanMemoryAllocator
    tinyobjloader::tinyobjloader
    glm::glm-header-only
    Microsoft::DirectXTK12
    meshoptimizer
    ${PXR_LIBRARIES}

    # FidelityFX SDK (For brixelizer)
    ${FFX_BACKEND_LIB}
    ${FFX_BRIXELIZER_LIB}
)

if (${USE_SUPERLUMINAL})
    target_link_libraries(${PROJECT_NAME} PRIVATE SuperluminalAPI)
endif()

# Defines
# --------------------------------

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    IMGUI_IMPL_VULKAN_USE_VOLK 
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
)

if (${USE_SUPERLUMINAL})
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SUPERLUMINAL)
endif()

if (${USE_VK_LABELS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_VK_LABELS)
endif()

# LivePP Configuration
# --------------------------------

# if (CMAKE_BUILD_TYPE STREQUAL "Debug")
#     target_compile_options     (${PROJECT_NAME} PRIVATE /Zi /Gm-)
#     target_link_options        (${PROJECT_NAME} PRIVATE /FUNCTIONPADMIN /OPT:NOREF /OPT:NOICF /DEBUG:FULL)
#     target_include_directories (${PROJECT_NAME} PRIVATE External/)
#     target_compile_definitions (${PROJECT_NAME} PRIVATE USE_LIVEPP)
# endif()