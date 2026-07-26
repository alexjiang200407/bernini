struct VSOut
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOut VSMain(uint index: SV_VertexID)
{
    float2 corners[3] = {
        float2(0.0, 0.8),
        float2(-0.8, -0.8),
        float2(0.8, -0.8),
    };

    VSOut output;
    output.position = float4(corners[index], 0.0, 1.0);
    return output;
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
