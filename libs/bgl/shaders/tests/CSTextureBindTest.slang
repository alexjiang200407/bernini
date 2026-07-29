// Plainly-bound resources, not the bindless .Handle forms: this is what WGSL reflection has to
// classify into a texture, a sampler and a storage-buffer bind-group entry from one Uniforms struct.
// The engine's own shaders still use the bindless forms; migrating those is a separate change.
//
// BoundTexture mirrors idl.TextureHandle's shape -- a struct whose first member is the resource --
// because that is the member Uniforms::operator=(TextureHandle) writes through.
struct BoundTexture
{
    Texture2D texture;

    float4 SampleLevel(SamplerState s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }
};

struct Uniforms
{
    BoundTexture texture;
    SamplerState sampler;
    RWStructuredBuffer<float4> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

// SampleLevel, not Sample: a compute stage has no derivatives to pick a mip from.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gUniforms.outBuffer[0] = gUniforms.texture.SampleLevel(gUniforms.sampler, float2(0.5, 0.5), 0.0);
}
