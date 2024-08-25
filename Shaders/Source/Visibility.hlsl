#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Constants
{
    float4x4 _MatrixMVP;
    uint     _MeshID;
    uint     _MeshCount;
};
[[vk::push_constant]] Constants gConstants;

struct VertexInput
{
    [[vk::location(0)]] float3 positionOS : POSITION;
};

float4 Vert(VertexInput input) : SV_Position
{
    return mul(gConstants._MatrixMVP, float4(input.positionOS, 1.0));;
}

/*
uint Frag(uint primitiveID : SV_PrimitiveID) : SV_Target
{
    // Warning: Bad things will happen for index count greater than 1 << 24u. 
    return gConstants._DrawCallID << 24u | primitiveID;
}
*/


float3 ColorCycle(uint index, uint count)
{
	float t = frac(index / (float)count);

	// source: https://www.shadertoy.com/view/4ttfRn
	float3 c = 3.0 * float3(abs(t - 0.5), t.xx) - float3(1.5, 1.0, 2.0);
	return 1.0 - c * c;
}

float4 Frag() : SV_Target
{
    return float4(ColorCycle(gConstants._MeshID, gConstants._MeshCount), 1);
}