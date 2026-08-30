import idl.IMaterial;
import idl.RawEntry;
import idl.TextureHandle;
import types.ComputeBuffer;
import types.RawHandleArena;

// Declares a raw arena and its typed handle view as ONE member, which is what Uniforms has to place
// for the pair to be assignable in a single write. Both halves are read so neither is stripped
// before it reaches the reflection.
struct Uniforms
{
    RawHandleArena<TextureHandle> arena;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let entry = RawEntry<IMaterial>.At(16u);

    gUniforms.outBuffer[0] = gUniforms.arena.LoadTag(entry);
    gUniforms.outBuffer[1] = uint(gUniforms.arena.GetHandleAt(16u, 0).Load(uint2(0, 0)).x);
}
