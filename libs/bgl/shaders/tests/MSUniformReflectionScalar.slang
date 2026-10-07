public struct AllScalarUniforms
{
    public float f1;
    public float2 f2;
    public float3 f3;
    public float4 f4;
    public int i1;
    public int2 i2;
    public int3 i3;
    public int4 i4;
    public uint u1;
    public uint2 u2;
    public uint3 u3;
    public uint4 u4;
    public float4x4 mat;
};

ConstantBuffer<AllScalarUniforms> gUniforms;

struct NoVertex
{
    float4 position : SV_Position;
    float value : TEXCOORD;
};

// Reflection only, so it draws nothing: one vertex and no primitive. SPIR-V requires a mesh stage
// to declare its outputs, and Slang emits a vertex of system values alone as an input, which DXIL
// refuses -- hence the user value.
[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main(OutputVertices<NoVertex, 1> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(1, 0);
    verts[0].position = float4(0.0, 0.0, 0.0, 1.0);
    verts[0].value = 0.0;
}
