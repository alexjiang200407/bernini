import types.SubmeshInstance;
import types.ComputeBuffer;
import types.PackedBuffer;
import idl.PsoType;
import idl.Constants;
import idl.InstanceVisibility;
import debug.dbg;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
    AtomicComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared Atomic<uint> gLocalHistogram[kPsoCount];

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    if (localIdx < kPsoCount)
    {
        gLocalHistogram[localIdx].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];

    if (!instance.meshInstance.Null() && gUniforms.visibility[globalIdx].visible != 0u)
    {
        let psoType = instance.pso;

        dbg_assert(psoType < kPsoCount, ErrorCode.kInvalidPsoType);

        if (psoType < kPsoCount)
        {
            gLocalHistogram[psoType].add(1u);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (localIdx < kPsoCount)
    {
        gUniforms.outBuffer[localIdx].add(gLocalHistogram[localIdx].load());
    }
}
