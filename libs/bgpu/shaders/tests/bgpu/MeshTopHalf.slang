// A quad over the upper half of clip space, y from 0 to 1, which D3D draws over a target's top
// rows: a readback of the first and last rows says which way up a backend draws.
struct VSOut
{
    float4 position : SV_Position;
    // DXC rejects a mesh output that is SV_Position alone: "invalid for shader model: ms".
    float2 uv : TEXCOORD;
};

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 4> verts, OutputIndices<uint3, 2> tris)
{
    SetMeshOutputCounts(4, 2);
    if (gtid < 4)
    {
        let corner = float2(gtid & 1u, gtid >> 1u);
        VSOut o;
        o.position = float4(corner.x * 2.0 - 1.0, corner.y, 0.5, 1.0);
        o.uv = corner;
        verts[gtid] = o;
    }
    if (gtid == 0)
    {
        tris[0] = uint3(0, 2, 1);
        tris[1] = uint3(1, 2, 3);
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
