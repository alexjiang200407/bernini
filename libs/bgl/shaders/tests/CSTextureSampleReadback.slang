import types.ComputeBuffer;

struct Uniforms
{
    Texture2D.Handle texture;
    SamplerState.Handle sampler;
    ComputeBuffer<float4> outColor;
};

ConstantBuffer<Uniforms> gUniforms;

// Samples a bindless texture through a bindless sampler and writes the texel where the CPU can read
// it. Unlike CSTextureSampleTest this reports the value rather than raising a GPU assertion, so it
// needs no debug-buffer support and checks what actually came back.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gUniforms.outColor[0] = gUniforms.texture.SampleLevel(gUniforms.sampler, float2(0.5, 0.5), 0.0);
}
