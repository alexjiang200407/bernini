import idl.ToonShadingRigBlock;
import lib.math.ToonShading;
import lib.types.ComputeBuffer;
import lib.types.RawBuffer;

// One evaluated edit's push at a head-space point, per case, for a CPU comparison against the same
// formula. A case is 128 bytes: the slot, then the point and its normal as float4s.
struct Uniforms
{
    RawBuffer records;
    ComputeBuffer<float4> outValues;
    uint caseCount;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    for (uint i = 0; i < gUniforms.caseCount; ++i)
    {
        let base = i * 128u;
        let slot = gUniforms.records.Load<ToonShadingRigSlot>(base);
        let p = gUniforms.records.Load<float4>(base + 96u).xyz;
        let n = gUniforms.records.Load<float4>(base + 112u).xyz;
        gUniforms.outValues[i] = float4(ToonShadingRigSlotOffset(slot, p, n), 0.0, 0.0, 0.0);
    }
}
