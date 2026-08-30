import types.RawBuffer;
import types.EntryBuffer;
import types.HandleElement;
import types.ComputeBuffer;
import idl.RawTextureHandle;

// A record with no resource type in it, so a raw Load<T> is legal on every backend. The eight bytes
// at `tex` are a texture handle's; only a typed view of the same allocation can sample them.
struct ViewedRecord
{
    float4 tint;
    RawTextureHandle tex;
};

struct Uniforms
{
    RawBuffer arena;                                       // the allocation, as bytes
    EntryBuffer<HandleElement<Texture2D.Handle> > handles; // the same allocation, as handles
    ComputeBuffer<float4> outValues;
    SamplerState.Handle samp;
    uint recordOffset;
    uint handleByteOffset;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let record = gUniforms.arena.Load<ViewedRecord>(gUniforms.recordOffset);
    gUniforms.outValues[0] = record.tint;

    // The raw view reads the handle's bytes as data...
    gUniforms.outValues[1] = float4(float(record.tex.bits.x), float(record.tex.bits.y), 0.0, 0.0);

    // ...and the typed view over those same bytes samples them as a texture.
    let tex = gUniforms.handles[gUniforms.handleByteOffset / 8u].handle;
    gUniforms.outValues[2] = tex.SampleLevel(gUniforms.samp, float2(0.5, 0.5), 0.0);
}
