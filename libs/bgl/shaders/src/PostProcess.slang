import idl.TextureHandle;
import util.Tonemap;

struct PostProcessData
{
    TextureHandle sceneColor;
    SamplerState.Handle sampler;
};

ConstantBuffer<PostProcessData> gPostProcessData;

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

// AgX leaves its result linear so the sRGB target re-encodes it. Alpha is forced opaque: the
// backbuffer is a display surface, and scene alpha is a surface's own coverage, not the pixel's --
// a hashed or cutout survivor carries its texture alpha, and passing that through hands a capture
// (and anything compositing the backbuffer) transparency the image does not have.
[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    float4 scene = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, input.uv, 0.0);

    return float4(AgX(scene.rgb), 1.0);
}
