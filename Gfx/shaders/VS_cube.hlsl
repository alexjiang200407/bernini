cbuffer CameraCB : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
};

cbuffer ModelCB : register(b0, space1)
{
    float4x4 modelTransform;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = float4(input.position, 1.0f);

    output.position = mul(projMatrix, mul(viewMatrix, worldPos));
    return output;
}
