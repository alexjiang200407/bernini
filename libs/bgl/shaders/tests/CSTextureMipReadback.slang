import idl.TextureHandle;
import types.ComputeBuffer;

struct Uniforms
{
    TextureHandle texture;
    SamplerState.Handle sampler;
    ComputeBuffer<float4> outColor;
    uint levels;
};

ConstantBuffer<Uniforms> gUniforms;

// One explicit-LOD sample per mip, written where the CPU can read them. An integer level through a
// linear mip filter reads that level alone, so each output is one mip's content with no blending --
// which is what lets a test tell "this mip was never uploaded / is unreachable" from "the implicit
// LOD never selects it".
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    for (uint level = 0; level < gUniforms.levels; ++level)
    {
        gUniforms.outColor[level] = gUniforms.texture.SampleLevel(gUniforms.sampler, float2(0.5, 0.5), float(level));
    }
}
