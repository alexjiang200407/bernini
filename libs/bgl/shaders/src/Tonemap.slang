import idl.TextureHandle;
import util.Tonemap;

struct TonemapData
{
    TextureHandle sceneColor;
    SamplerState.Handle sampler;
};

ConstantBuffer<TonemapData> gTonemapData;

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

// AgX leaves its result linear so the sRGB target re-encodes it; alpha is passed through because the
// capture path reads it back.
[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    float4 scene = gTonemapData.sceneColor.SampleLevel(gTonemapData.sampler, input.uv, 0.0);

    return float4(AgX(scene.rgb), scene.a);
}
