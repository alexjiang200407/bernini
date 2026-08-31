import lib.util.Tonemap;

struct PostProcessData
{
    Texture2D.Handle sceneColor;

    // Point when the source is already on the backbuffer's grid, linear when a render-resolution
    // scene colour has to be carried across -- which is only a frame the TAA resolve did not run.
    SamplerState.Handle sampler;

    Texture2D.Handle outlineMask;

    // The mask is on the render grid and the dilate below is a coverage test, so this one is always
    // a point sampler however the scene colour is filtered.
    SamplerState.Handle maskSampler;

    float2 maskSize;
    uint outlineEnabled;
};

ConstantBuffer<PostProcessData> gPostProcessData;

// Display-space: the outline is editor feedback, composited after the tonemap so exposure and the
// display curve cannot shift it.
static const float3 c_OutlineColor = float3(1.0, 0.5, 0.0);

// The width is authored at c_OutlineRefHeightPx and scaled by the mask's height, so the outline
// keeps its share of the frame rather than a texel count: the mask is on the render grid and the
// image is reconstructed onto a denser or coarser output one, and a fixed texel count would arrive
// proportionally thicker.
static const float c_OutlineWidthPx = 4.0;
static const float c_OutlineRefHeightPx = 2160.0;

// The dilate costs (2r+1)^2 taps, so past this the outline stops thickening rather than the frame
// paying quadratically for a supersampled target.
static const int c_MaxOutlineWidthPx = 8;

// A pixel is outline when it is outside the mask but within the outline width of it -- a dilate of
// the mask minus the mask itself, which contours the silhouette without tinting the surface.
float SampleOutline(float2 uv)
{
    if (gPostProcessData.outlineMask.SampleLevel(gPostProcessData.maskSampler, uv, 0.0).r > 0.0)
    {
        return 0.0;
    }

    const float2 texelSize = 1.0 / gPostProcessData.maskSize;
    const float width = c_OutlineWidthPx * gPostProcessData.maskSize.y / c_OutlineRefHeightPx;

    // Floored at one texel: below it the outline would disappear, and selection feedback nobody can
    // see is worse than one that is a touch thick.
    const int radius = clamp(int(round(width)), 1, c_MaxOutlineWidthPx);

    float covered = 0.0;

    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            float2 offset = float2(dx, dy) * texelSize;
            covered = max(covered, gPostProcessData.outlineMask.SampleLevel(gPostProcessData.maskSampler, uv + offset, 0.0).r);
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
