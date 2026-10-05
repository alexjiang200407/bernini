import lib.math.ColorGrade;
import lib.math.hash;
import lib.math.rcas;
import lib.math.Tonemap;
import lib.screen.TonemapLut;

struct PostProcessData
{
    Texture2D.Handle sceneColor;

    // Point when the source is already on the backbuffer's grid, linear when a render-resolution
    // scene colour has to be carried across -- which is only a frame the TAA resolve did not run --
    // or when the colour split reads it between texels.
    SamplerState.Handle sampler;

    // The resolved history's texel, for RCAS's cross; RCAS runs only on a frame the resolve ran, when
    // the source is on this pass's grid and point-sampled. Zero strength skips it.
    float2 sourceTexelSize;
    float rcasStrength;

    Texture2D.Handle outlineMask;

    // The mask is on the render grid and the dilate below is a coverage test, so this one is always
    // a point sampler however the scene colour is filtered.
    SamplerState.Handle maskSampler;

    float2 maskSize;
    uint outlineEnabled;

    // The display curve's LUT and the linear clamp it is read through.
    Texture2D.Handle tonemapLut;
    SamplerState.Handle lutSampler;

    // The bloom chain's finished level 0, on the source's grid at half resolution -- always
    // linearly sampled, whatever carries the scene colour across.
    Texture2D.Handle bloom;
    SamplerState.Handle bloomSampler;
    float bloomIntensity;
    uint bloomEnabled;

    // lib.math.ColorGrade's fields, flat because a cbuffer member is addressed by one name.
    float3 gradeWhiteBalance;
    float3 gradeSlope;
    float3 gradeOffset;
    float3 gradePower;
    float gradeSaturation;
    float gradeContrast;
    float gradeVignetteIntensity;
    float gradeVignetteSmoothness;
    uint gradeEnabled;

    // The toon post-process: Blender's Standard view, the exposed value clamped and no curve.
    uint toon;

    // Where red is read from, relative to green, in uv: `splitOffset` everywhere, plus
    // `splitRadial` per unit of distance from the centre. Blue is read from the opposite side.
    float2 splitOffset;
    float splitRadial;
    uint splitEnabled;

    // Film grain: the pattern's pitch in output pixels, at least one; the largest share of its value
    // a pixel moves by; and which pattern this frame shows.
    float grainPitch;
    float grainIntensity;
    uint grainPattern;
    uint grainEnabled;
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

// The glow over a display value with no curve to roll it off: the sum where both are small, and
// short of white however bright either is. Written so that no glow returns `base` exactly.
float3 ScreenBlend(float3 base, float3 layer)
{
    return base + layer * (1.0 - base);
}

// Triangular in (-1, 1): the sum of two uniforms, which has film grain's soft-shouldered
// distribution where one uniform is flat to its edges.
float GrainCell(int2 cell, uint pattern)
{
    const uint h = HashUint(uint(cell.x) ^ HashUint(uint(cell.y) ^ HashUint(pattern)));
    return HashFloat(h) + HashFloat(h ^ 0x9e3779b9u) - 1.0;
}

// Bilinear between cells `pitch` pixels apart, so a pitch above one is soft rather than blocky. At
// a pitch of one a pixel's centre is its cell's and the weights are exactly one and zero.
float Grain(float2 pixelCentre, float pitch, uint pattern)
{
    const float2 p = pixelCentre / pitch - 0.5;
    const float2 base = floor(p);
    const float2 f = p - base;
    const int2 cell = int2(base);

    const float top = lerp(GrainCell(cell, pattern), GrainCell(cell + int2(1, 0), pattern), f.x);
    const float bottom = lerp(GrainCell(cell + int2(0, 1), pattern), GrainCell(cell + int2(1, 1), pattern), f.x);

    return lerp(top, bottom, f.y);
}

ColorGrade TargetGrade()
{
    ColorGrade grade;
    grade.whiteBalance = gPostProcessData.gradeWhiteBalance;
    grade.slope = gPostProcessData.gradeSlope;
    grade.offset = gPostProcessData.gradeOffset;
    grade.power = gPostProcessData.gradePower;
    grade.saturation = gPostProcessData.gradeSaturation;
    grade.contrast = gPostProcessData.gradeContrast;
    grade.vignetteIntensity = gPostProcessData.gradeVignetteIntensity;
    grade.vignetteSmoothness = gPostProcessData.gradeVignetteSmoothness;
    return grade;
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

// The scene at `uv`, sharpened on a frame that asks for it. The sharpen is of the resolved scene,
// not of the glow combined with it afterwards.
float3 SampleScene(float2 uv)
{
    float3 scene = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, uv, 0.0).rgb;

    if (gPostProcessData.rcasStrength > 0.0)
    {
        let texel = gPostProcessData.sourceTexelSize;
        float3 above = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, uv - float2(0.0, texel.y), 0.0).rgb;
        float3 left = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, uv - float2(texel.x, 0.0), 0.0).rgb;
        float3 right = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, uv + float2(texel.x, 0.0), 0.0).rgb;
        float3 below = gPostProcessData.sceneColor.SampleLevel(gPostProcessData.sampler, uv + float2(0.0, texel.y), 0.0).rgb;

        scene = Rcas(above, left, scene, right, below, gPostProcessData.rcasStrength);
    }

    return scene;
}

// AgX and Standard leave their result linear so the sRGB target re-encodes it. Alpha is forced opaque: the
// backbuffer is a display surface, and scene alpha is a surface's own coverage, not the pixel's --
// a hashed or cutout survivor carries its texture alpha, and passing that through hands a capture
// (and anything compositing the backbuffer) transparency the image does not have.
[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    float3 scene = SampleScene(input.uv);

    // Ahead of the curve, where a lens puts it; toon's curve is a per-channel clamp, so there it
    // is a split of the displayed image exactly. The glow is too soft to show one.
    if (gPostProcessData.splitEnabled != 0)
    {
        const float2 red = gPostProcessData.splitOffset + (input.uv - 0.5) * gPostProcessData.splitRadial;

        scene.r = SampleScene(input.uv - red).r;
        scene.b = SampleScene(input.uv + red).b;
    }

    // Linear radiance. Filmic adds it ahead of the curve, which rolls it off; toon has no curve.
    float3 glow = float3(0.0, 0.0, 0.0);

    if (gPostProcessData.bloomEnabled != 0)
    {
        glow = gPostProcessData.bloomIntensity * gPostProcessData.bloom.SampleLevel(gPostProcessData.bloomSampler, input.uv, 0.0).rgb;
    }

    TonemapLut lut;
    lut.strip = gPostProcessData.tonemapLut;
    lut.sampler = gPostProcessData.lutSampler;

    float3 color;

    if (gPostProcessData.toon != 0)
    {
        color = ScreenBlend(saturate(scene), saturate(glow));

        if (gPostProcessData.gradeEnabled != 0)
        {
            color = ToonGraded(color, input.uv, TargetGrade());
        }
    }
    else if (gPostProcessData.gradeEnabled != 0)
    {
        color = AgXGraded(scene + glow, input.uv, TargetGrade(), lut);
    }
    else
    {
        color = AgX(scene + glow, lut);
    }

    // A share of the displayed value, so black stays black and a lifted black carries a little.
    if (gPostProcessData.grainEnabled != 0)
    {
        color *=
            1.0 + gPostProcessData.grainIntensity * Grain(input.position.xy, gPostProcessData.grainPitch, gPostProcessData.grainPattern);
    }

    if (gPostProcessData.outlineEnabled != 0)
    {
        color = lerp(color, c_OutlineColor, SampleOutline(input.uv));
    }

    return float4(color, 1.0);
}
