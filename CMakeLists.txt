cmake_minimum_required(VERSION 3.10)

# Build System
# --------------------------------

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/External/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "")

set(PROJECT_NAME Vulkan-RayTraced-Indirect)

project(${PROJECT_NAME} VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Packages
# --------------------------------

find_package(spdlog                REQUIRED)
find_package(Stb                   REQUIRED)
find_package(volk                  REQUIRED)
find_package(glfw3                 REQUIRED)
find_package(VulkanMemoryAllocator REQUIRED)
find_package(tinyobjloader         REQUIRED)
find_package(glm                   REQUIRED)

# Executable
# --------------------------------

add_executable(${PROJECT_NAME} 
    Source/Main.cpp
)

# Include
# --------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})

# Link
# --------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE 
    volk::volk_headers 
    spdlog::spdlog_header_only
    glfw
    GPUOpen::VulkanMemoryAllocator
    tinyobjloader::tinyobjloader
    glm::glm-header-only
)