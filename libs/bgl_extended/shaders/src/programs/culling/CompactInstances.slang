import idl.Constants;
import idl.DispatchArgs;
import idl.InstanceVisibility;
import idl.PsoType;
import lib.types.ComputeBuffer;
import lib.types.PackedBuffer;
import lib.data.SubmeshInstance;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
    ComputeBuffer<uint> psoPrefixSum;
    ComputeBuffer<uint> compactedInstances;
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared Atomic<uint> gGroupCount[kPsoCount];
groupshared uint gGroupBase[kPsoCount];

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    if (localIdx < kPsoCount)
    {
        gGroupCount[localIdx].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];
    let psoType = instance.pso;

    // Skip exactly what the histogram skipped, or the bucket bases and the scatter disagree.
    let scatters = !instance.meshInstance.Null() && psoType < kPsoCount && gUniforms.visibility[globalIdx].visible != 0u;

    // No early-out past this point: a thread that does not scatter must still reach every
    // GroupMemoryBarrierWithGroupSync below, or the ones that do scatter hang on it.
    uint groupSlot = 0u;
    if (scatters)
    {
        groupSlot = gGroupCount[psoType].add(1u);
    }
    GroupMemoryBarrierWithGroupSync();

    // One reservation per bucket per group, and none at all for a bucket this group did not fill.
    // Reserving per instance instead makes threadCountX a single contended address -- a scene drawn
    // from one pso serializes its every instance on it.
    if (localIdx < kPsoCount)
    {
        let count = gGroupCount[localIdx].load();
        gGroupBase[localIdx] = (count != 0u) ? gUniforms.dispatchArgs[localIdx].threadCountX.add(count) : 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    if (scatters)
    {
        let base = (psoType == 0u) ? 0u : gUniforms.psoPrefixSum[psoType - 1u];
        gUniforms.compactedInstances[base + gGroupBase[psoType] + groupSlot] = globalIdx;
    }
}
