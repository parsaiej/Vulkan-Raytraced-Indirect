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

uint Frag(uint primitiveID : SV_PrimitiveID) : SV_Target
{
    // Warning: Bad things will happen for index count greater than 1 << 24u. 
    return gConstants._MeshID << 24u | primitiveID;
}