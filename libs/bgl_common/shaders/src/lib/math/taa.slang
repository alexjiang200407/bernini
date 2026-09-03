// The temporal antialiasing resolve and the math it is built from, generic over what it samples
// (idl.IResolveInputs) so it names no texture. See docs/taa.md for the recipe.
import idl.IResolveInputs;

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

// Catmull-Rom over the 4x4 around `uv`, folded to five bilinear fetches by pairing the two inner
// taps per axis and dropping the four corners, whose weights are near zero (renormalised out).
//
// A plain bilinear fetch convolves the history with a tent every frame it lands off a texel
// centre, which under sustained motion is every frame -- the accumulation re-blurs each hop and
// the image softens until the camera stops. The negative lobes here undo that. They can also
// overshoot, so the result is only safe once the neighbourhood clamp has pulled it inside the box.
float3 SampleHistoryCatmullRom<I : IResolveInputs>(I inputs, float2 uv, float2 texel)
{
    float2 samplePos = uv / texel;
    float2 innerTexel = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - innerTexel;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    // The two inner taps collapse to one bilinear fetch placed where their weights balance.
    float2 w12 = w1 + w2;
    float2 uv0 = (innerTexel - 1.0) * texel;
    float2 uv3 = (innerTexel + 2.0) * texel;
    float2 uv12 = (innerTexel + w2 / w12) * texel;

    // Fetched first so the weighted sum lowers to one expression, the shape every measurement in
    // docs/taa.md was made on.
    float3 top = inputs.History(float2(uv12.x, uv0.y));
    float3 left = inputs.History(float2(uv0.x, uv12.y));
    float3 middle = inputs.History(uv12);
    float3 right = inputs.History(float2(uv3.x, uv12.y));
    float3 bottom = inputs.History(float2(uv12.x, uv3.y));

    float3 sum = top * (w12.x * w0.y) + left * (w0.x * w12.y) + middle * (w12.x * w12.y) + right * (w3.x * w12.y) + bottom * (w12.x * w3.y);

    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;

    return sum / weight;
}

// How far the surface at `uv` moved on its own, in render texels: the written vector less what the
// camera alone accounts for at the surface's depth.
float ObjectTexels<I : IResolveInputs>(I inputs, ResolveParams p, float2 uv, float2 motion, float2 texel)
{
    if (p.cameraPairValid == 0.0)
    {
        return 0.0;
    }

    // The far plane is the sky or nothing: neither animates, and at infinity a translation's
    // parallax and an emptied pixel's zero would both read as motion of their own.
    const float c_FarPlane = 0.99999;

    float depth = inputs.Depth(uv);
    if (depth >= c_FarPlane)
    {
        return 0.0;
    }

    return length((motion - CameraMotion(uv, depth, p.jitter, p.clipToView, p.viewToPrevClip)) / texel);
}

/**
 * The resolve for the output pixel at `uv`: the render 3x3 bounds the history admitted, the
 * neighbour moving most on its own lends its vector, and this frame is blended in by how near its
 * jittered sample landed. docs/taa.md is the recipe and the measurement behind each choice.
 *
 * @return The new history at `uv`, alpha zero.
 */
public float4 TaaResolve<I : IResolveInputs>(I inputs, ResolveParams p, float2 uv)
{
    let texel = p.renderTexelSize;

    // Where this output pixel sits on the render grid, and the render pixel whose jittered sample
    // landed nearest to it -- every sample of a frame shares one offset, so the nearest is found by
    // rounding rather than searched for.
    float2 renderPos = uv * p.renderSize;
    float2 samplePixel = floor(renderPos + p.jitterTexels) + 0.5;

    // One render sample per output pixel makes the pixel's own texel the nearest sample and the
    // interpolated uv its centre already. Reconstructing that centre instead would land a rounding
    // step away, which the neighbourhood clamp and the motion discriminator both read -- and that
    // step is the entire difference between this resolve and the render-grid one it replaces.
    bool resampling = p.resampling != 0.0;

    float2 sampleUv = resampling ? samplePixel * texel : uv;
    float2 sampleOffset = resampling ? (renderPos - samplePixel) + p.jitterTexels : p.jitterTexels;

    float4 current = inputs.Scene(sampleUv);

    float2 motion = inputs.Motion(sampleUv);

    // What a surface must move by on its own before a neighbour's vector is worth borrowing: a
    // twentieth of a texel doubles no edge, and clears the reconstruction's residue on a static
    // surface -- measured bit-identical under an orbit -- by an order of magnitude.
    const float c_ObjectTexels = 0.05;

    // The 3x3 the history is admitted from: variance bounds, always. On stochastic coverage a
    // min/max box spans both populations whole and admits every dragged mixture -- the smear a
    // hashed surface trails at a distance -- where the sigma box recentres on the majority.
    // The trade-off history is in docs/taa.md.
    //
    // The same 3x3 yields the vector of whichever neighbour moves most on its own, which stands in
    // for the surface at a silhouette (docs/taa.md).
    float3 centre = RgbToYCoCg(current.rgb);
    float3 boxMin = centre;
    float3 boxMax = centre;
    float3 m1 = centre;
    float3 m2 = centre * centre;
    float2 dilatedMotion = motion;
    float dilatedObjectTexels = max(ObjectTexels(inputs, p, sampleUv, motion, texel), c_ObjectTexels);

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            float2 tapUv = sampleUv + float2(x, y) * texel;
            float3 tap = RgbToYCoCg(inputs.Scene(tapUv).rgb);

            boxMin = min(boxMin, tap);
            boxMax = max(boxMax, tap);
            m1 += tap;
            m2 += tap * tap;

            float2 tapMotion = inputs.Motion(tapUv);

            float tapObjectTexels = ObjectTexels(inputs, p, tapUv, tapMotion, texel);
            if (tapObjectTexels > dilatedObjectTexels)
            {
                dilatedObjectTexels = tapObjectTexels;
                dilatedMotion = tapMotion;
            }
        }
    }

    float2 historyUv = uv - dilatedMotion;

    // The sigma box is kept inside the min/max box: nine bounded samples can put mu +/- sigma
    // outside it, and outside it the clamp would admit colours the neighbourhood never produced.
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mu * mu, 0.0));
    boxMin = clamp(mu - sigma, boxMin, boxMax);
    boxMax = clamp(mu + sigma, boxMin, boxMax);

    // Reprojected off-screen: there is no history for this pixel, and wrapping the clamp sampler's
    // edge texel in would be a smear rather than an accumulation.
    bool onScreen = all(historyUv >= 0.0) && all(historyUv <= 1.0);

    // Branched, not folded into the blend weight below: the history texture is not cleared, so on the
    // first frame it holds whatever the allocation did, which may decode as NaN. `lerp(NaN, c, 1.0)`
    // is NaN -- the zero coefficient does not discard the operand -- and a NaN written into the
    // accumulation stays there for the life of the target.
    if (!onScreen || p.historyValid == 0.0)
    {
        // Filtered onto the output grid rather than point-taken: there is no accumulation to build
        // the frame out of jittered samples yet, so this one frame is a plain resample.
        float3 whole = resampling ? inputs.SceneLinear(uv) : current.rgb;

        return float4(whole, 0.0);
    }

    float3 history = SampleHistoryCatmullRom(inputs, historyUv, p.outputTexelSize);

    // Clamped rather than rejected: a pixel whose history is merely stale still carries subpixel
    // detail worth keeping once it is pulled inside what this frame could have produced.
    history = YCoCgToRgb(clamp(RgbToYCoCg(history), boxMin, boxMax));

    // On a grid denser than the render one, an output pixel no sample landed near this frame takes
    // almost none of it and waits for the phase that serves it.
    float sampleWeight = PhaseWeight(sampleOffset.x, p.jitterTexels.x, p.subPixels.x, p.sampleWeightK) *
                         PhaseWeight(sampleOffset.y, p.jitterTexels.y, p.subPixels.y, p.sampleWeightK);

    float weight = sampleWeight * p.blendWeight;

    float3 resolved = UnToneWeight(lerp(ToneWeight(history), ToneWeight(current.rgb), weight));

    return float4(resolved, 0.0);
}
