import idl.CullView;
import idl.VatState;
import lib.types.ComputeBuffer;
import lib.types.RawBuffer;

// Loads out of one raw buffer, reported for a CPU comparison against the bytes it wrote.
//
// The two record loads answer whether a raw Load<T> lays T out the way bgl_idlgen's C++ mirror
// asserts: VatState is a real playback record of packed scalars, CullView adds a matrix and a
// fixed array, which are where a target's own packing rules would show up first.
//
// The loose loads answer the other half: a vertex attribute is only ever 4-byte aligned, so both
// come from an address that is 4-aligned and deliberately not 16-aligned.
struct Uniforms
{
    RawBuffer records;
    ComputeBuffer<float4> outValues;
    uint stateOffset;
    uint viewOffset;
    uint vertexOffset;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let state = gUniforms.records.Load<VatState>(gUniforms.stateOffset);
    gUniforms.outValues[0] = float4(float(state.geom.GetOffset()), float(state.clip), state.phase, state.rate);

    let view = gUniforms.records.Load<CullView>(gUniforms.viewOffset);
    gUniforms.outValues[1] = view.frustumPlanes[3];
    gUniforms.outValues[2] = view.viewProj[2];

    gUniforms.outValues[3] = gUniforms.records.Load<float4>(gUniforms.vertexOffset);
    gUniforms.outValues[4] = float4(gUniforms.records.Load<float3>(gUniforms.vertexOffset + 16u), 0.0);
}
