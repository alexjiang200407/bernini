import lib.types.ComputeBuffer;
import lib.math.ColorGrade;
import lib.screen.TonemapLut;

struct Uniforms
{
    ComputeBuffer<float4> outColor;
    float3 sceneLinear;
    float2 uv;

    // The shipped AgX strip, bound by RunProbe in util/AgxProbe.cpp; the grade itself has no LUT.
    Texture2D.Handle lut;
    SamplerState.Handle lutSampler;

    float3 whiteBalance;
    float3 slope;
    float3 offset;
    float3 power;
    float saturation;
    float contrast;
    float vignetteIntensity;
    float vignetteSmoothness;
};

ConstantBuffer<Uniforms> gUniforms;

// The graded tone map PostProcess runs, over one colour at one output position, so each control's
// direction can be read off a number rather than a screenshot.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    ColorGrade grade;
    grade.whiteBalance = gUniforms.whiteBalance;
    grade.slope = gUniforms.slope;
    grade.offset = gUniforms.offset;
    grade.power = gUniforms.power;
    grade.saturation = gUniforms.saturation;
    grade.contrast = gUniforms.contrast;
    grade.vignetteIntensity = gUniforms.vignetteIntensity;
    grade.vignetteSmoothness = gUniforms.vignetteSmoothness;

    TonemapLut lut;
    lut.strip = gUniforms.lut;
    lut.sampler = gUniforms.lutSampler;
    gUniforms.outColor[0] = float4(AgXGraded(gUniforms.sceneLinear, gUniforms.uv, grade, lut), 1.0);
}
