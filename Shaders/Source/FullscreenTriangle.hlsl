#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Interpolators
{
    float4 positionCS : SV_Position;
    float2 texCoord   : TEXCOORD0;
};

Interpolators Main(uint vertexID : SV_VertexID)
{
    Interpolators i;
    {
        i.positionCS = GetFullScreenTriangleVertexPosition(vertexID);
        i.texCoord   = GetFullScreenTriangleTexCoord(vertexID);
    }
    return i;
}