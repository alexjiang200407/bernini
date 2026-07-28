// A mesh pipeline with nothing else in it: one group emits the covering triangle and the pixel stage
// writes solid white, so a readback of every texel is the whole assertion. Lives here rather than in
// FullscreenRect.slang because the mesh stage is no longer part of that path -- the fullscreen passes
// draw through the vertex entry on both backends now.

struct VSOut
{
    float4 position : SV_Position;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        float2 uv = float2((gtid << 1) & 2, gtid & 2);

        VSOut vout;
        vout.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
