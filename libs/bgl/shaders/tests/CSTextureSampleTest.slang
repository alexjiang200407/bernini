import debug.dbg;
import idl.ErrorCode;
import idl.TextureHandle;
import idl.SamplerHandle;

struct Uniforms
{
    TextureHandle texture;
    SamplerHandle sampler;
};

ConstantBuffer<Uniforms> gUniforms;

// Samples a bindless texture and raises a GPU assertion when the texel is non-null.
// The CPU test uploads a known non-zero texel, so a fired assertion proves the SRV
// resolved and the upload landed; no assertion means the bindless texture path failed.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float4 color = gUniforms.texture.SampleLevel(gUniforms.sampler, float2(0.5, 0.5), 0.0);

    if (any(color != float4(0.0, 0.0, 0.0, 0.0)))
    {
        dbg_raise(ErrorCode.kUnknown);
    }
}
