
cbuffer TestCBuffer : register(b0)
{
    float4 testColor;
};

cbuffer TestMaterialCBuffer : register(b3, space3)
{
    float4 a;
    float2 b;
    float c;
    float d;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

float4 PS_Cbuf_test(PSInput input) : SV_TARGET
{
    return float4(testColor.r, testColor.g, a.r, 1.0);
}
