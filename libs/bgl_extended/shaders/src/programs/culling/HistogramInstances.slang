import idl.Constants;
import idl.InstanceVisibility;
import idl.PsoType;
import lib.debug.dbg;
import lib.types.ComputeBuffer;
import lib.types.PackedBuffer;
import lib.data.SubmeshInstance;

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
    AtomicComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared Atomic<uint> gLocalHistogram[cMaxPsoBuckets];

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    for (uint i = localIdx; i < cMaxPsoBuckets; i += cHistogramGroupSize)
    {
        gLocalHistogram[i].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];

    if (!instance.meshInstance.Null() && gUniforms.visibility[globalIdx].visible != 0u)
    {
        let psoType = instance.pso;

        dbg_assert(psoType < cMaxPsoBuckets, ErrorCode.kInvalidPsoType);

        if (psoType < cMaxPsoBuckets)
        {
            gLocalHistogram[psoType].add(1u);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Skip the empty rows: every group flushing zero adds to the same 1 KB region is the
    // worst-contention case, not a free one.
    for (uint i = localIdx; i < cMaxPsoBuckets; i += cHistogramGroupSize)
    {
        let count = gLocalHistogram[i].load();
        if (count != 0u)
        {
            gUniforms.outBuffer[i].add(count);
        }
    }
}
