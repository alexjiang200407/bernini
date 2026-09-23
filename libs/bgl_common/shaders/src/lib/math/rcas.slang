// FSR 1's RCAS (ffx_fsr1.h FsrRcasF) with FSR 2's noise detection on; docs/taa.md § The sharpen.

// Where the limiter stops: FSR 1's FSR_RCAS_LIMIT, at the edge of unnatural results.
static const float c_RcasLimit = 0.25 - 1.0 / 16.0;

float Max3(float a, float b, float c)
{
    return max(a, max(b, c));
}

float Min3(float a, float b, float c)
{
    return min(a, min(b, c));
}

/**
 * The sharpened centre `e` of the cross `b` (above), `d` (left), `f` (right), `h` (below).
 *
 * @param strength The linear lobe scale, exp2(-stops); 1 is RCAS at its maximum.
 */
public float3 Rcas(float3 b, float3 d, float3 e, float3 f, float3 h, float strength)
{
    // Luma times two, FSR 1's weighting.
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    // A centre that stands alone against a flat ring is grain rather than an edge, and gets half the
    // lobe.
    float range = Max3(Max3(bL, dL, eL), fL, hL) - Min3(Min3(bL, dL, eL), fL, hL);
    float noise = 0.25 * (bL + dL + fL + hL) - eL;
    noise = saturate(abs(noise) / max(range, 1e-6));
    noise = -0.5 * noise + 1.0;

    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));

    // FSR 1 divides through a hardware reciprocal, and a zero denominator would be a NaN here: a black
    // ring, or a ring exactly at the peak.
    float3 hitMin = mn4 / max(4.0 * mx4, 1e-6);
    float3 peakDenominator = 4.0 * mn4 - 4.0;
    peakDenominator = select(abs(peakDenominator) < 1e-6, -1e-6, peakDenominator);
    float3 hitMax = (1.0 - mx4) / peakDenominator;
    float3 lobes = max(-hitMin, hitMax);
    float lobe = max(-c_RcasLimit, min(Max3(lobes.r, lobes.g, lobes.b), 0.0)) * strength * noise;

    return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}
