#define SHADER_API_VULKAN
#include "ShaderLibrary/Common.hlsl"

struct Constants
{
    float4x4 _MatrixVP;
    uint     DebugModeValue;
    uint     MeshCount;
};
[[vk::push_constant]] Constants gConstants;

struct Interpolators
{
    float4 positionCS : SV_Position;
    float2 texCoord   : TEXCOORD0;
};

struct DrawItemMetaData
{
    float4x4 matrixM;
    uint     faceCount;
    uint     materialIndex;
    uint2    unused;
};

[[vk::binding(0, 0)]]
Texture2D<uint> _VisibilityBuffer;

[[vk::binding(1, 0)]]
Texture2D<float> _DepthBuffer;

[[vk::binding(0, 1)]]
ByteAddressBuffer _IndexBuffers[];

[[vk::binding(1, 1)]]
ByteAddressBuffer _VertexBuffers[];

[[vk::binding(2, 1)]]
StructuredBuffer<DrawItemMetaData> _DrawItemMetaData;

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
        
    // Decode mesh and triangle data. 
    const uint meshIndex = visibility >> 16U;
    const uint primIndex = visibility & 0xFFFF;

    return float4(ColorCycle(primIndex % 3, 3), 1);
}

#include "Barycentric.hlsl"

float4 DebugBarycentricCoordinate(Interpolators i)
{
    uint visibility = _VisibilityBuffer.Load(uint3(i.positionCS.xy, 0));

    if (!visibility)
        return 0;

    // Decode mesh and triangle data. 
    const uint meshIndex = visibility >> 16U;
    const uint primIndex = visibility & 0xFFFF;

    // Load primitive indices.
    uint3 indices = _IndexBuffers[NonUniformResourceIndex(meshIndex)].Load3(12U * primIndex);

    // Load points.
    float3 positionOS0 = asfloat(_VertexBuffers[NonUniformResourceIndex(meshIndex)].Load3(12U * indices.x));
    float3 positionOS1 = asfloat(_VertexBuffers[NonUniformResourceIndex(meshIndex)].Load3(12U * indices.y));
    float3 positionOS2 = asfloat(_VertexBuffers[NonUniformResourceIndex(meshIndex)].Load3(12U * indices.z));

    // Construct the final matrix.
    float4x4 matrixMVP = mul(gConstants._MatrixVP, _DrawItemMetaData[meshIndex].matrixM);

    // Compute homogenous coordinates.
    float4 positionCS0 = mul(matrixMVP, float4(positionOS0, 1.0));
    float4 positionCS1 = mul(matrixMVP, float4(positionOS1, 1.0));
    float4 positionCS2 = mul(matrixMVP, float4(positionOS2, 1.0));

    // Need to flip the pixel coordinate. 
    i.texCoord = float2(i.texCoord.x, 1 - i.texCoord.y);

    // Compute barycentric coordinates.
    Barycentric::Data barycentrics = Barycentric::Compute(positionCS0, positionCS1, positionCS2, -1 + 2 * i.texCoord, float2(1920, 1080));

    return float4(barycentrics.m_lambda, 1);
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