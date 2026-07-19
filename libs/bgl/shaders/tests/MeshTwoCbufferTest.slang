struct VSOut
{
    float4 position : SV_Position;
    float value : TEXCOORD; // un-numbered: Slang's numbered [[user]] names differ mesh vs fragment
};

// Two constant buffers with disjoint stage usage: gMesh is read only by the mesh stage, gFrag only
// by the fragment. This pins the binding-index invariant the meshlet pipeline relies on -- that a
// cbuffer's reflected index (from the composed program) equals the [[buffer(N)]] each per-stage MSL
// places it at. If it did not, the mesh would read gFrag's bytes (or vice versa) and a channel would
// be wrong.
struct MeshData
{
    float meshValue;
};

struct FragData
{
    float4 fragColor;
};

ConstantBuffer<MeshData> gMesh;
ConstantBuffer<FragData> gFrag;

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        float2 uv = float2((gtid << 1) & 2, gtid & 2);
        VSOut o;
        o.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
        o.value = gMesh.meshValue;
        verts[gtid] = o;
    }
    if (gtid == 0)
        tris[0] = uint3(0, 1, 2);
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(input.value, gFrag.fragColor.y, gFrag.fragColor.z, 1.0);
}
