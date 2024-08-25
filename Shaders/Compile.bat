@echo off

if "%1"=="Vert" (
    C:\VulkanSDK\1.3.283.0\Bin\dxc.exe -E Main -T vs_6_1 -spirv -fspv-target-env=vulkan1.3 -Fo Compiled\\%2.vert.spv Source\\%2.hlsl
)

if "%1"=="Frag" (
    C:\VulkanSDK\1.3.283.0\Bin\dxc.exe -E Main -T ps_6_1 -spirv -fspv-target-env=vulkan1.3 -Fo Compiled\\%2.frag.spv Source\\%2.hlsl
)

if "%1"=="Ray" (
    C:\VulkanSDK\1.3.283.0\Bin\dxc.exe -E Main -T lib_6_3 -spirv -fspv-target-env=vulkan1.3 -Fo Compiled\\%2.spv Source\\%2.hlsl
)