import lib.types.ComputeBuffer;

// Samples one texel of a 2x2 texture per thread, through a sampler, both reached by handle: a
// readback proves the texture and sampler descriptors a backend writes are the ones a shader finds.
struct Uniforms
{
    Texture2D<float4>.Handle texture;
    SamplerState.Handle pointSampler;
    ComputeBuffer<float4> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(4, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    let uv = (float2(idx & 1u, idx >> 1u) + 0.5) / 2.0;
    gUniforms.outBuffer[idx] = gUniforms.texture.SampleLevel(gUniforms.pointSampler, uv, 0.0);
}
