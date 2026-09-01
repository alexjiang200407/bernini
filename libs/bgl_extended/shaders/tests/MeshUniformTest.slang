struct VSOut
{
    float4 position : SV_Position;
    // Un-numbered semantic: Slang names a numbered [[user]] inconsistently between the mesh output
    // and the fragment input, so the Metal stage-linkage would reject TEXCOORD0.
    float value : TEXCOORD;
};

// Exercised by both stages: the mesh shader reads meshValue and passes it through to the fragment,
// which reads fragColor. A wrong binding in either stage shows up as a wrong output channel.
struct Uniforms
{
    float meshValue;
    float4 fragColor;
};

ConstantBuffer<Uniforms> gUniforms;

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
        o.value = gUniforms.meshValue;
        verts[gtid] = o;
    }
    if (gtid == 0)
        tris[0] = uint3(0, 1, 2);
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return float4(input.value, gUniforms.fragColor.y, gUniforms.fragColor.z, 1.0);
}
