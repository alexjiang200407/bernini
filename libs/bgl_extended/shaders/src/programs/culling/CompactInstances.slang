import idl.Constants;
import idl.DispatchArgs;
import idl.InstanceVisibility;
import idl.DrawBucket;
import lib.types.ComputeBuffer;
import lib.types.PackedBuffer;
import lib.data.SubmeshInstance;

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
    ComputeBuffer<uint> drawBucketPrefixSum;
    ComputeBuffer<uint> compactedInstances;
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared Atomic<uint> gGroupCount[cMaxDrawBuckets];
groupshared uint gGroupBase[cMaxDrawBuckets];

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    for (uint i = localIdx; i < cMaxDrawBuckets; i += cCompactGroupSize)
    {
        gGroupCount[i].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];
    let bucket = instance.drawBucket;

    // Skip exactly what the histogram skipped, or the bucket bases and the scatter disagree.
    let scatters = !instance.meshInstance.Null() && bucket < cMaxDrawBuckets && gUniforms.visibility[globalIdx].visible != 0u;

    // No early-out past this point: a thread that does not scatter must still reach every
    // GroupMemoryBarrierWithGroupSync below, or the ones that do scatter hang on it.
    uint groupSlot = 0u;
    if (scatters)
    {
        groupSlot = gGroupCount[bucket].add(1u);
    }
    GroupMemoryBarrierWithGroupSync();

    // One reservation per bucket per group, and none at all for a bucket this group did not fill.
    // Reserving per instance instead makes threadCountX a single contended address -- a scene drawn
    // from one bucket serializes its every instance on it.
    for (uint i = localIdx; i < cMaxDrawBuckets; i += cCompactGroupSize)
    {
        let count = gGroupCount[i].load();
        gGroupBase[i] = (count != 0u) ? gUniforms.dispatchArgs[i].threadCountX.add(count) : 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    if (scatters)
    {
        let base = (bucket == 0u) ? 0u : gUniforms.drawBucketPrefixSum[bucket - 1u];
        gUniforms.compactedInstances[base + gGroupBase[bucket] + groupSlot] = globalIdx;
    }
}
