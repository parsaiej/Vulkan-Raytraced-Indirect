struct Constants
{
    float4x4 _MatrixM;
    float4x4 _MatrixVP;
    float4x4 _MatrixV;
    uint     _HasMaterial;
};
[[vk::push_constant]] Constants gConstants;

struct Interpolators
{
    float4 positionCS : SV_Position;
    float3 positionVS : TEXCOORD0;
};

struct RasterData
{
    float3 barycentric : SV_Barycentrics;
    uint   primitiveID : SV_PrimitiveID;
};

[[vk::binding(0u, 0u)]]
Texture2D _AlbedoImage;

[[vk::binding(1u, 0u)]]
Texture2D _NormalImage;

[[vk::binding(2u, 0u)]]
Texture2D _MetallicImage;

[[vk::binding(3u, 0u)]]
Texture2D _RoughnessImage;

[[vk::binding(4u, 0u)]]
SamplerState _DefaultSampler;

[[vk::binding(0u, 1u)]]
ByteAddressBuffer _FaceVaryingTextureCoordinateBuffer;

float3 ComputeScreenSpaceNormal(float3 positionVS) 
{
    float3 dx = ddx(positionVS);
    float3 dy = -ddy(positionVS);
    return normalize(cross(dx, dy));
}

float4 Main(Interpolators interpolators, RasterData rasterData) : SV_Target
{
    if (!gConstants._HasMaterial)
        return float4(0, 0, 0, 1);

    float2 st0 = asfloat(_FaceVaryingTextureCoordinateBuffer.Load2((rasterData.primitiveID * 3U + 0U) << 3U));
    float2 st1 = asfloat(_FaceVaryingTextureCoordinateBuffer.Load2((rasterData.primitiveID * 3U + 1U) << 3U));
    float2 st2 = asfloat(_FaceVaryingTextureCoordinateBuffer.Load2((rasterData.primitiveID * 3U + 2U) << 3U));
    
    float2 st = rasterData.barycentric.x * st0 + rasterData.barycentric.y * st1 + rasterData.barycentric.z * st2;

    st = float2(st.x, 1.0 - st.y);

    return sqrt(_AlbedoImage.Sample(_DefaultSampler, st));
}