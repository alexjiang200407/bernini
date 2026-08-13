import debug.dbg;
import idl.Entry;
import idl.Range;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.RangeBuffer;

// Dereferences a null Entry and a null Range on purpose. Offset 0 is the reserved element, so both
// reads are in bounds and return zeroes -- the dbg_assert inside Get is the only thing that reports
// them, which is the whole point of this shader.
struct Uniforms
{
    EntryBuffer<uint> entries;
    RangeBuffer<uint> ranges;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    Entry<uint> nullEntry = {};
    Range<uint> nullRange = {};

    // Stored, or the loads dead-strip and take the asserts inside them along.
    gUniforms.outBuffer[idx] = gUniforms.entries.Get(nullEntry) + gUniforms.ranges.Get(nullRange, 0u);
}
