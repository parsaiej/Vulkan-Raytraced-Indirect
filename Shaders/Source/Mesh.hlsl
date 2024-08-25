#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Constants
{
    float4x4 _MatrixM;
    float4x4 _MatrixVP;
    float4x4 _MatrixV;
    uint     _HasMaterial;
};
[[vk::push_constant]] Constants gConstants;

struct VertexInput
{
    [[vk::location(0)]] float3 positionOS : POSITION;
};

struct Interpolators
{
    float4 positionCS : SV_Position;
    float3 positionVS : TEXCOORD0;
};

Interpolators Main(VertexInput input)
{
    Interpolators i;
    ZERO_INITIALIZE(Interpolators, i);

    float3 positionWS = mul(gConstants._MatrixM, float4(input.positionOS, 1.0)).xyz;

    i.positionCS = mul(gConstants._MatrixVP, float4(positionWS, 1.0));
    i.positionVS = mul(gConstants._MatrixV,  float4(positionWS, 1.0)).xyz;
    
    return i;
}