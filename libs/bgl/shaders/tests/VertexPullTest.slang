// Vertex pulling: the vertex shader reads its positions from a bound storage buffer indexed by the
// vertex id, rather than from a vertex buffer -- the shape the forward path uses. A read-only
// StructuredBuffer (not the RW ComputeBuffer) because WebGPU forbids a read_write storage binding in
// the vertex stage. This is a WebGPU-only test source, so it needs no BGL_WGSL guard.

struct VSOut
{
    float4 position : SV_Position;
};

struct PullData
{
    StructuredBuffer<float2> positions;
};

ConstantBuffer<PullData> gPull;

[shader("vertex")]
VSOut VSMain(uint vid: SV_VertexID)
{
    VSOut output;
    output.position = float4(gPull.positions[vid], 0.0, 1.0);
    return output;
}

[shader("pixel")]
float4 PSMain() : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
