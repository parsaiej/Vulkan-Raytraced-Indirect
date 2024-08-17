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

if (${USE_SUPERLUMINAL})
    # Warning: Superluminal ships with file named FindSuperluminalAPI.cmake, it needs to be renamed to SuperluminalAPIConfig.cmake.
    find_package(SuperluminalAPI REQUIRED)
endif()

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
    Source/Scene.cpp
)

target_precompile_headers(${PROJECT_NAME} PRIVATE Source/Include/Precompiled.h)

# Include
# --------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE
    Source/Include
    ${Stb_INCLUDE_DIR}
    ${PXR_INCLUDE_DIR}
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
    ${PXR_LIBRARIES}
)

if (${USE_SUPERLUMINAL})
    target_link_libraries(${PROJECT_NAME} PRIVATE SuperluminalAPI)
endif()


# Defines
# --------------------------------

target_compile_definitions(${PROJECT_NAME} PRIVATE _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

if (${USE_SUPERLUMINAL})
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SUPERLUMINAL)
endif()

# LivePP Configuration
# --------------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options     (${PROJECT_NAME} PRIVATE /Zi /Gm-)
    target_link_options        (${PROJECT_NAME} PRIVATE /FUNCTIONPADMIN /OPT:NOREF /OPT:NOICF /DEBUG:FULL)
    target_include_directories (${PROJECT_NAME} PRIVATE External/)
    target_compile_definitions (${PROJECT_NAME} PRIVATE USE_LIVEPP)
endif()