import types.RawBuffer;
import types.ComputeBuffer;
import idl.RawEntry;
import idl.RawRange;

// Reads back what a RawBuffer wrote: two records of different kinds and one headerless range. What
// this pins is the seam -- the CPU's idea of where a header ends and a payload begins against the
// shader's, which is ADR-7's layout and nothing else checks it.
struct Uniforms
{
    RawBuffer arena;
    ComputeBuffer<float4> outValues;
    uint recordA;
    uint recordB;
    uint range;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let a = RawEntry<float4>.At(gUniforms.recordA);
    let b = RawEntry<float4>.At(gUniforms.recordB);

    gUniforms.outValues[0] = gUniforms.arena.LoadRecordAs<float4>(a);
    gUniforms.outValues[1] = gUniforms.arena.LoadRecordAs<float4>(b);
    gUniforms.outValues[2] = float4(float(gUniforms.arena.LoadTag(a)), float(gUniforms.arena.LoadTag(b)), 0.0, 0.0);
    gUniforms.outValues[3] = gUniforms.arena.LoadInRange<float4>(RawRange.At(gUniforms.range), 0u);
}
