// The program that writes the LUT PbrShading samples; the integral itself is lib.math.brdf_lut.
import lib.math.brdf_lut;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        VSOut vout;
        vout.uv = float2((gtid << 1) & 2, gtid & 2);
        vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

// u is dot(N,V) and v is roughness, matching how PbrShading samples the result. Both are taken at
// the texel centre, so the row for roughness 0 is the one a mirror reads.
[shader("pixel")]
float2 PSMain(VSOut input) : SV_Target
{
    return IntegrateBRDF(max(input.uv.x, 1e-4), input.uv.y);
}
