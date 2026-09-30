import lib.types.ComputeBuffer;
import lib.math.Tonemap;
import lib.screen.TonemapLut;

struct Uniforms
{
    ComputeBuffer<float4> outColor;
    float sceneLinear;
    Texture2D.Handle lut;
    SamplerState.Handle lutSampler;
};

ConstantBuffer<Uniforms> gUniforms;

// Runs the shipped tone map over a grey and reports what it produced, so the CPU can check where
// middle grey lands. Deliberately calls AgX itself rather than reproducing it: the point is that
// this shader and PostProcess use the same code.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float3 grey = float3(gUniforms.sceneLinear, gUniforms.sceneLinear, gUniforms.sceneLinear);
    TonemapLut lut;
    lut.strip = gUniforms.lut;
    lut.sampler = gUniforms.lutSampler;
    gUniforms.outColor[0] = float4(AgX(grey, lut), 1.0);
}
