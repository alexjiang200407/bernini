import types.ComputeBuffer;
import types.HandleElement;

struct Uniforms
{
    // The handle under test lives in this buffer, not in the cbuffer: a cbuffer handle is rewritten
    // per dispatch, so it proves nothing about one the CPU wrote into GPU memory once.
    ComputeBuffer<HandleElement<Texture2D.Handle> > materials;
    SamplerState.Handle sampler;
    ComputeBuffer<float4> outColor;
};

ConstantBuffer<Uniforms> gUniforms;

// Samples through a texture handle read out of GPU memory and reports the texel. Companion to
// CSTextureSampleReadback, which samples through a cbuffer handle: the two differ only in where the
// handle came from, which is the whole point.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gUniforms.outColor[0] = gUniforms.materials[0].handle.SampleLevel(gUniforms.sampler, float2(0.5, 0.5), 0.0);
}
