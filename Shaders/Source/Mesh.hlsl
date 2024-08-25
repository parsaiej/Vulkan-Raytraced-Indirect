#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Constants
{
    float4x4 _MatrixM;
    float4x4 _MatrixVP;
    float4x4 _MatrixV;
};
[[vk::push_constant]] Constants gConstants;

struct VertexInput
{
    [[vk::location(0)]] float3 positionOS : POSITION;
    [[vk::location(1)]] float3 normalOS   : NORMAL;
    [[vk::location(2)]] float2 texCoord0  : TEXCOORD0;
};

struct Interpolators
{
    float4 positionCS : SV_Position;
    float3 positionVS : TEXCOORD0;
    float3 normalOS   : TEXCOORD1;
    float2 texCoord0  : TEXCOORD2;
};

Interpolators Main(VertexInput input)
{
    Interpolators i;
    ZERO_INITIALIZE(Interpolators, i);

    float3 positionWS = mul(gConstants._MatrixM, float4(input.positionOS, 1.0)).xyz;

    i.positionCS = mul(gConstants._MatrixVP, float4(positionWS, 1.0));
    i.positionVS = mul(gConstants._MatrixV,  float4(positionWS, 1.0)).xyz;
    i.normalOS   = mul(gConstants._MatrixM,  float4(input.normalOS, 0.0)).xyz;
    i.texCoord0  = float2(input.texCoord0.x, 1.0 - input.texCoord0.y);
    
    return i;
}