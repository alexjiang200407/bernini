struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// The covering triangle's corner, shared by both entry points so they cannot drift.
VSOut EvaluateCorner(uint corner)
{
    VSOut vout;
    vout.uv = float2((corner << 1) & 2, corner & 2);
    vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return vout;
}

[shader("vertex")]
VSOut VSMain(uint vertexId: SV_VertexID)
{
    return EvaluateCorner(vertexId);
}

// Retained until the D3D12 backend implements IGraphicsPipeline; the vertex entry above is what
// both backends will use. Absent on WGSL, which has no mesh stage and rejects the entry at load.
#if !defined(BGL_WGSL)

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        verts[gtid] = EvaluateCorner(gtid);
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

#endif

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
