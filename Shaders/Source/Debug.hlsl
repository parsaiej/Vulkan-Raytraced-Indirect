#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Constants
{
    uint DebugModeValue;
    uint MeshCount;
};
[[vk::push_constant]] Constants gConstants;

struct Interpolators
{
    float4 positionCS : SV_Position;
    float2 texCoord   : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D<uint> _VisibilityBuffer;

[[vk::binding(1, 0)]]
Texture2D<float> _DepthBuffer;

[[vk::binding(0, 1)]]
ByteAddressBuffer _MeshBuffers[];

[[vk::binding(1, 1)]]
ByteAddressBuffer _MeshMetadataBuffer;

float3 ColorCycle(uint index, uint count)
{
	float t = frac(index / (float)count);

	// source: https://www.shadertoy.com/view/4ttfRn
	float3 c = 3.0 * float3(abs(t - 0.5), t.xx) - float3(1.5, 1.0, 2.0);
	return 1.0 - c * c;
}

float4 DebugMeshID(Interpolators i)
{
    uint visibility = _VisibilityBuffer.Load(uint3(i.positionCS.xy, 0));

    if (!visibility)
        return 0;

    return float4(ColorCycle(visibility >> 16U, gConstants.MeshCount), 1);
}

float4 DebugPrimitiveID(Interpolators i)
{
    uint visibility = _VisibilityBuffer.Load(uint3(i.positionCS.xy, 0));

    if (!visibility)
        return 0;

    return float4(ColorCycle(visibility & 0xFFFF, 0xF), 1);
}

float4 DebugBarycentricCoordinate(Interpolators i)
{
    uint visibility = _VisibilityBuffer.Load(uint3(i.positionCS.xy, 0));

    // Decode mesh and triangle data. 
    const uint meshIndex = visibility >> 16U;
    const uint primIndex = visibility & 0xFFFF;

    // Dummy load from a buffer.
    uint data = _MeshBuffers[NonUniformResourceIndex(0U)].Load(0U);

    return float4(data.xxx, 1);
}

float4 DebugDepth(Interpolators i)
{
    return _DepthBuffer.Load(uint3(i.positionCS.xy, 0)).xxxx;
}

float4 Frag(Interpolators i) : SV_Target
{
    switch(gConstants.DebugModeValue)
    {
        case 1:
            return DebugMeshID(i);
        case 2:
            return DebugPrimitiveID(i);
        case 3:
            return DebugBarycentricCoordinate(i);
        case 4:
            return DebugDepth(i);
        break;
    }

    return float4(1, 0, 0, 1);
}