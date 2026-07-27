// A constant buffer mixing plain data with a pulled vertex stream, read from the vertex stage --
// the shape ForwardData has. The transform is applied through a matrix so the std140 matrix storage
// WGSL uses is exercised, not just scalars.

struct DrawData
{
    float4x4 transform;
    float2 offset;
    StructuredBuffer<float2> positions;
};

ConstantBuffer<DrawData> gDraw;

struct VSOut
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOut VSMain(uint vid: SV_VertexID)
{
    VSOut output;
    float4 local = float4(gDraw.positions[vid] + gDraw.offset, 0.0, 1.0);
    output.position = mul(gDraw.transform, local);
    return output;
}

[shader("pixel")]
float4 PSMain() : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
