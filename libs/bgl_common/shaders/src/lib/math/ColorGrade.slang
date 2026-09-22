import idl.ITonemapLut;
import lib.math.Tonemap;

// A frame grade around AgX: white balance and a vignette in scene linear, then the ASC CDL and
// contrast in the log coordinate the formation LUT reads. Every field's neutral value is the
// identity. See docs/passes.md.
public struct ColorGrade
{
    // CAT02 LMS gains, derived from temperature and tint on the CPU.
    public float3 whiteBalance;

    public float3 slope;
    public float3 offset;
    public float3 power;
    public float saturation;
    public float contrast;

    public float vignetteIntensity;
    public float vignetteSmoothness;
};

// Unity's constants: a corner darkens fully at intensity 1, and smoothness 0.2 is a linear falloff.
static const float c_VignetteReach = 3.0;
static const float c_VignetteFalloff = 5.0;

// The CDL's saturation is defined against Rec.709 luma whatever space it runs in.
static const float3 c_Rec709Luma = float3(0.2126, 0.7152, 0.0722);

public float3 WhiteBalance(float3 color, float3 lmsGain)
{
    // Linear Rec.709 <-> CAT02 LMS, column-vector layout.
    const float3x3 rec709ToLms =
        float3x3(3.90405e-1, 5.49941e-1, 8.92632e-3, 7.08416e-2, 9.63172e-1, 1.35775e-3, 2.31082e-2, 1.28021e-1, 9.36245e-1);
    const float3x3 lmsToRec709 =
        float3x3(2.85847e+0, -1.62879e+0, -2.48910e-2, -2.10182e-1, 1.15820e+0, 3.24281e-4, -4.18120e-2, -1.18169e-1, 1.06867e+0);

    return mul(lmsToRec709, mul(rec709ToLms, color) * lmsGain);
}

// `uv` is the pixel's position on the output, [0, 1] across it.
public float Vignette(float2 uv, float intensity, float smoothness)
{
    const float2 d = abs(uv - 0.5) * (intensity * c_VignetteReach);
    return pow(saturate(1.0 - dot(d, d)), smoothness * c_VignetteFalloff);
}

public float3 ApplyCdl(float3 coord, float3 slope, float3 offset, float3 power, float saturation)
{
    const float3 graded = pow(max(coord * slope + offset, 0.0), power);
    const float luma = dot(graded, c_Rec709Luma);
    return luma + saturation * (graded - luma);
}

// About middle grey's coordinate, so a contrast change leaves the image's exposure where it was.
public float3 ApplyContrast(float3 coord, float contrast)
{
    const float pivot = AgXLogEncode(float3(0.18, 0.18, 0.18)).g;
    return (coord - pivot) * contrast + pivot;
}

public float3 AgXGraded<L : ITonemapLut>(float3 color, float2 uv, ColorGrade grade, L lut)
{
    float3 sceneLinear = WhiteBalance(color, grade.whiteBalance);
    sceneLinear *= Vignette(uv, grade.vignetteIntensity, grade.vignetteSmoothness);

    float3 coord = AgXLogEncode(sceneLinear);
    coord = ApplyCdl(coord, grade.slope, grade.offset, grade.power, grade.saturation);
    coord = ApplyContrast(coord, grade.contrast);

    return AgXFormation(coord, lut);
}
