// The temporal antialiasing math the resolve is built from, kept apart from the sampling it runs
// on. See docs/taa.md for the recipe.

// Chrominance-decorrelated luma space. The neighbourhood bounds are an axis-aligned box, and in RGB
// that box is a poor fit to the set of colours an edge actually produces -- it admits history the
// pixel could not have come from, which is what ghosting is.
public float3 RgbToYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

public float3 YCoCgToRgb(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Weighting the blend by inverse luma stops one bright sample from dominating the average, and
// undoing it afterwards keeps the result linear. Identity below a luma of 1: weighting each
// operand by its own luma converges a bimodal signal below its true mean, which on stochastic
// coverage is the strand fading with distance, so only genuine HDR outliers pay the compression.
// The forward map takes a max channel of m to 2m / (1 + m) above the knee, so it stays below 2
// and the inverse below stays finite.
public float3 ToneWeight(float3 c)
{
    float m = max(max(c.r, c.g), c.b);
    return m <= 1.0 ? c : c * (2.0 / (1.0 + m));
}

public float3 UnToneWeight(float3 c)
{
    float n = max(max(c.r, c.g), c.b);
    return n <= 1.0 ? c : c / max(1e-4, 2.0 - n);
}

// How much of this frame an output pixel takes along one axis, given how far its centre sits from
// where the frame's sample landed -- divided by the same kernel averaged over the sub-pixel phases
// one render sample serves.
//
// The division is what the accumulation depends on. Undivided, the kernel would scale the blend by
// the jitter's phase even where there is nothing to choose between, and at one output pixel per
// render pixel -- every render scale of 1.0 -- it would change an image nothing else here changes.
// Normalized, that case is a single phase whose weight is its own mean, which is exactly one, and
// the kernel only ever redistributes a frame's weight between the sub-pixels sharing its sample.
//
// Separable, and normalized per axis rather than once over the pair, because a kernel normalized
// once over the pair could not reduce to unity per axis: exp(-a)*exp(-b) is not exp(-(a+b)).
public float PhaseWeight(float offset, float jitterTexel, float subPixels, float sampleWeightK)
{
    // The width the caller asks for is in *output* pixels, which is render pixels only where the two
    // grids coincide. A kernel kept at render width would let an output pixel take a strong share of
    // a sample a whole output pixel away, and reconstruct the frame softer than a plain filtered
    // upscale of it -- measured, and the reason the sub-pixel count is in here.
    // One phase weighs itself against its own mean, so the answer is one and no width can move it.
    // Returned rather than divided out: a backend free to implement the division as an approximate
    // reciprocal makes x/x land a bit either side of one, and that bit is the whole of what a render
    // scale of 1.0 would otherwise show for a setting that cannot reach it.
    if (subPixels <= 1.0)
    {
        return 1.0;
    }

    float k = sampleWeightK * subPixels * subPixels;

    float sum = 0.0;
    for (float i = 0.0; i < subPixels; i += 1.0)
    {
        float d = (i + 0.5) / subPixels - 0.5 + jitterTexel;
        sum += exp(-k * d * d);
    }

    return exp(-k * offset * offset) * subPixels / sum;
}

// The motion the camera alone gives a surface at `depth` behind `uv`, in the velocity buffer's
// convention; a written vector less this is the surface's own motion (docs/taa.md).
public float2 CameraMotion(float2 uv, float depth, float2 jitter, float4x4 clipToView, float4x4 viewToPrevClip)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) - jitter;
    float4 view = mul(clipToView, float4(ndc, depth, 1.0));
    view /= view.w;

    float4 prevClip = mul(viewToPrevClip, view);
    float2 prevNdc = prevClip.xy / prevClip.w;

    return (ndc - prevNdc) * float2(0.5, -0.5);
}
