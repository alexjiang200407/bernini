
struct PSInput
{
    float4 position : SV_POSITION;
};

float4 PS_GBuffer_Opaque_PBR(PSInput input) : SV_TARGET
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
