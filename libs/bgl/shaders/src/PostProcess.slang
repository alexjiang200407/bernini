import idl.TextureHandle;
import util.Tonemap;

struct PostProcessData
{
    TextureHandle sceneColor;
    SamplerState.Handle sampler;
    TextureHandle outlineMask;
    float2 maskTexelSize;
    uint outlineEnabled;
};

ConstantBuffer<PostProcessData> gPostProcessData;

// Display-space: the outline is editor feedback, composited after the tonemap so exposure and the
// display curve cannot shift it.
static const float3 c_OutlineColor = float3(1.0, 0.5, 0.0);
static const int c_OutlineWidthPx = 4;

// A pixel is outline when it is outside the mask but within c_OutlineWidthPx of it -- a dilate of
// the mask minus the mask itself, which contours the silhouette without tinting the surface.
float SampleOutline(float2 uv)
{
    if (gPostProcessData.outlineMask.SampleLevel(gPostProcessData.sampler, uv, 0.0).r > 0.0)
    {
        return 0.0;
    }

    float covered = 0.0;

    [unroll]
    for (int dy = -c_OutlineWidthPx; dy <= c_OutlineWidthPx; ++dy)
    {
        [unroll]
        for (int dx = -c_OutlineWidthPx; dx <= c_OutlineWidthPx; ++dx)
        {
            float2 offset = float2(dx, dy) * gPostProcessData.maskTexelSize;
            covered = max(covered, gPostProcessData.outlineMask.SampleLevel(gPostProcessData.sampler, uv + offset, 0.0).r);
        }
    }

    return covered > 0.0 ? 1.0 : 0.0;
}

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

    float3 color = AgX(scene.rgb);

    if (gPostProcessData.outlineEnabled != 0)
    {
        color = lerp(color, c_OutlineColor, SampleOutline(input.uv));
    }

    return float4(color, 1.0);
}
