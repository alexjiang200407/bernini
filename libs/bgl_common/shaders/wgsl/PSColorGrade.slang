// WGSL-lowering driver for lib.math.ColorGrade. Fragment stage because that is where the renderer
// runs it (programs/screen/PostProcess.slang). The LUT is a plain 2D texture, as in PSTonemap.
import idl.ITonemapLut;
import lib.math.ColorGrade;
import lib.math.Tonemap;

Texture2D gLut;
SamplerState gLinear;

struct BoundLut : ITonemapLut
{
    float3 Sample(float3 coord)
    {
        uint width;
        uint height;
        gLut.GetDimensions(width, height);
        let taps = StripLutTaps3D(coord, height);
        return lerp(gLut.SampleLevel(gLinear, taps.uv0, 0.0).rgb, gLut.SampleLevel(gLinear, taps.uv1, 0.0).rgb, taps.blend);
    }
};

[shader("fragment")]
float4 main(float3 color: COLOR, float2 uv: TEXCOORD) : SV_Target
{
    ColorGrade grade;
    grade.whiteBalance = float3(1.0, 1.0, 1.0);
    grade.slope = float3(1.0, 1.0, 1.0);
    grade.offset = float3(0.0, 0.0, 0.0);
    grade.power = float3(1.0, 1.0, 1.0);
    grade.saturation = 1.0;
    grade.contrast = 1.0;
    grade.vignetteIntensity = 0.0;
    grade.vignetteSmoothness = 0.2;

    BoundLut lut;
    return float4(AgXGraded(color, uv, grade, lut), 1.0);
}
