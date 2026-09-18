struct BloomDownsampleData
{
    Texture2D.Handle source;
    SamplerState.Handle sampler;

    float2 sourceTexelSize;

    // The prefilter curve, applied on the first level only: knee is threshold * softKnee,
    // precomputed so the shader never divides by a user value.
    float threshold;
    float knee;

    uint isFirstLevel;
};

ConstantBuffer<BloomDownsampleData> gBloomDownsampleData;

struct BloomUpsampleData
{
    Texture2D.Handle coarse;
    Texture2D.Handle fine;
    SamplerState.Handle sampler;

    float2 coarseTexelSize;
    float scatter;
};

ConstantBuffer<BloomUpsampleData> gBloomUpsampleData;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

float Luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Quadratic soft knee: zero below threshold - knee, the full excess above threshold + knee. At
// threshold 0 the curve degenerates to identity, which is what lets "bloom everything" be a
// setting rather than a branch.
float3 Prefilter(float3 color)
{
    const float brightness = max(color.r, max(color.g, color.b));
    const float knee = gBloomDownsampleData.knee;

    float soft = clamp(brightness - gBloomDownsampleData.threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);

    const float contribution = max(soft, brightness - gBloomDownsampleData.threshold);

    return color * (contribution / max(brightness, 1e-4));
}

float3 SampleSource(float2 uv, float2 offsetInTexels)
{
    const float2 coord = uv + offsetInTexels * gBloomDownsampleData.sourceTexelSize;
    float3 color = gBloomDownsampleData.source.SampleLevel(gBloomDownsampleData.sampler, coord, 0.0).rgb;

    if (gBloomDownsampleData.isFirstLevel != 0)
    {
        color = Prefilter(color);
    }

    return color;
}

// One quad of the 13-tap kernel. On the first level each quad is averaged with Karis weights, so
// a single out-of-range sample -- a firefly the jitter moves every frame -- cannot own the whole
// mip chain; below the first level the input is already band-limited and the weighting would only
// darken it.
float3 QuadAverage(float3 a, float3 b, float3 c, float3 d)
{
    if (gBloomDownsampleData.isFirstLevel == 0)
    {
        return (a + b + c + d) * 0.25;
    }

    const float wa = 1.0 / (1.0 + Luma(a));
    const float wb = 1.0 / (1.0 + Luma(b));
    const float wc = 1.0 / (1.0 + Luma(c));
    const float wd = 1.0 / (1.0 + Luma(d));

    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

// The 13-tap downsample: four overlapping corner quads at +-2 texels weighted an eighth each, and
// the centre quad at +-1 weighted a half. Written against the *source* grid, so one pass halves
// the resolution without shimmer under motion.
[shader("pixel")]
float4 PSDownsample(VSOut input) : SV_Target
{
    const float2 uv = input.uv;

    const float3 a = SampleSource(uv, float2(-2.0, -2.0));
    const float3 b = SampleSource(uv, float2(0.0, -2.0));
    const float3 c = SampleSource(uv, float2(2.0, -2.0));
    const float3 d = SampleSource(uv, float2(-2.0, 0.0));
    const float3 e = SampleSource(uv, float2(0.0, 0.0));
    const float3 f = SampleSource(uv, float2(2.0, 0.0));
    const float3 g = SampleSource(uv, float2(-2.0, 2.0));
    const float3 h = SampleSource(uv, float2(0.0, 2.0));
    const float3 i = SampleSource(uv, float2(2.0, 2.0));

    const float3 j = SampleSource(uv, float2(-1.0, -1.0));
    const float3 k = SampleSource(uv, float2(1.0, -1.0));
    const float3 l = SampleSource(uv, float2(-1.0, 1.0));
    const float3 m = SampleSource(uv, float2(1.0, 1.0));

    float3 color = QuadAverage(a, b, d, e) * 0.125;
    color += QuadAverage(b, c, e, f) * 0.125;
    color += QuadAverage(d, e, g, h) * 0.125;
    color += QuadAverage(e, f, h, i) * 0.125;
    color += QuadAverage(j, k, l, m) * 0.5;

    return float4(color, 1.0);
}

float3 SampleCoarse(float2 uv, float2 offsetInTexels)
{
    const float2 coord = uv + offsetInTexels * gBloomUpsampleData.coarseTexelSize;
    return gBloomUpsampleData.coarse.SampleLevel(gBloomUpsampleData.sampler, coord, 0.0).rgb;
}

// The 9-tap tent over the coarser level, folded into this level's downsample by scatter -- a lerp
// rather than an add, so the chain conserves energy and intensity stays the one brightness knob.
[shader("pixel")]
float4 PSUpsample(VSOut input) : SV_Target
{
    const float2 uv = input.uv;

    float3 tent = SampleCoarse(uv, float2(-1.0, -1.0));
    tent += SampleCoarse(uv, float2(0.0, -1.0)) * 2.0;
    tent += SampleCoarse(uv, float2(1.0, -1.0));
    tent += SampleCoarse(uv, float2(-1.0, 0.0)) * 2.0;
    tent += SampleCoarse(uv, float2(0.0, 0.0)) * 4.0;
    tent += SampleCoarse(uv, float2(1.0, 0.0)) * 2.0;
    tent += SampleCoarse(uv, float2(-1.0, 1.0));
    tent += SampleCoarse(uv, float2(0.0, 1.0)) * 2.0;
    tent += SampleCoarse(uv, float2(1.0, 1.0));
    tent *= 1.0 / 16.0;

    const float3 fine = gBloomUpsampleData.fine.SampleLevel(gBloomUpsampleData.sampler, uv, 0.0).rgb;

    return float4(lerp(fine, tent, gBloomUpsampleData.scatter), 1.0);
}
