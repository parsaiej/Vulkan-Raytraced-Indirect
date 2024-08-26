// Include 
// ---------------------------------

#include "Barycentric.hlsl"

// Constants
// ---------------------------------

struct Constants
{
    float4x4 _MatrixMVP;
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
ByteAddressBuffer _HomogenousCoordinateBuffers[];

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
    uint meshIndex = visibilityData >> 24u;
    uint primIndex = visibilityData & 0xFFFFFF;

    // Read off the mesh meta-data.
    uint2 meshMetaData = _MeshMetadatas.Load2(meshIndex << 3u);

    // Read off the triangle indices.
    uint3 triangleIndices = _IndexBuffers[NonUniformResourceIndex(meshMetaData.x)].Load3((3u * primIndex) << 4u);

    // Construct the barycentric coordinate + partial derivatives.
    // TODO

    // Force compiler to resolve all instructions while drafting.
    _DummyOutput.Store3(0u, triangleIndices);
}