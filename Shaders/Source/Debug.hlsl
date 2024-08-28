
struct Interpolators
{
    float4 positionCS : SV_Position;
    float2 texCoord   : TEXCOORD0;
};

float4 Frag(Interpolators i) : SV_Target
{
    return float4(i.texCoord, 0, 1);
}