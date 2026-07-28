struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint vertexId: SV_VertexID)
{
    VSOut vout;
    vout.uv = float2((vertexId << 1) & 2, vertexId & 2);
    vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return vout;
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
