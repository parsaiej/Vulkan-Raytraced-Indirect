// Include 
// ---------------------------------

#include "Barycentric.hlsl"

// Constants
// ---------------------------------

struct Constants
{
    float2 _ViewportSize;
};
[[vk::push_constant]] Constants gConstants;

// Inputs
// ---------------------------------

// Mesh Meta-data Schema:
// 0..4 Byte -> Index Start
// 4..8 Byte -> Position Start
// TODO the rest...

[[vk::binding(0, 0)]]
Texture2D<uint> _VisibilityBuffer;

[[vk::binding(1, 0)]]
ByteAddressBuffer _MeshMetadatas;

[[vk::binding(2, 0)]]
ByteAddressBuffer _IndexBuffers[];

[[vk::binding(3, 0)]]
ByteAddressBuffer _PositionBuffers[];

// NOTE: For now the below buffer lists are of USD Face-varying primvars. Sample accordingly!

[[vk::binding(4, 0)]]
ByteAddressBuffer _NormalBuffers[];

[[vk::binding(5, 0)]]
ByteAddressBuffer _TexcoordBuffers[];

// Outputs
// ---------------------------------

[[vk::binding(0, 1)]]
RWByteAddressBuffer _DummyOutput;

// Implementation
// ---------------------------------

[numthreads(1, 1, 1)]
void Main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Read off the visibility sample data.
    uint visibilityData = _VisibilityBuffer.Load(uint3(dispatchThreadID.xy, 0u));

    // Decode the mesh index and primitive index.
    uint meshIndex = visibilityData >> 16u;
    uint primIndex = visibilityData & 0xFFFF;

    // Read off the mesh meta-data.
    uint2 meshMetaData = _MeshMetadatas.Load2(meshIndex << 3u);

    // Read off the triangle indices.
    uint3 triangleIndices = _IndexBuffers[NonUniformResourceIndex(meshMetaData.x)].Load3((3u * primIndex) << 2u);

    // Load triangle positions.
    float4 positionH0 = _PositionBuffers[NonUniformResourceIndex(meshMetaData.y)].Load4(triangleIndices.x << 4u);
    float4 positionH1 = _PositionBuffers[NonUniformResourceIndex(meshMetaData.y)].Load4(triangleIndices.y << 4u);
    float4 positionH2 = _PositionBuffers[NonUniformResourceIndex(meshMetaData.y)].Load4(triangleIndices.z << 4u);

    // Compute the barycentric coordinate + partial derivatives.
    Barycentric::Data barycentric = Barycentric::Compute(positionH0, positionH1, positionH2, float2(0, 0), gConstants._ViewportSize);

    // Force compiler to resolve all instructions while drafting.
    _DummyOutput.Store3(0u, triangleIndices);
}