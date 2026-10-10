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

groupshared Atomic<uint> gGroupCount[cMaxDrawLanes];
groupshared uint gGroupBase[cMaxDrawLanes];

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    for (uint i = localIdx; i < cMaxDrawLanes; i += cCompactGroupSize)
    {
        gGroupCount[i].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];
    let bucket = instance.drawBucket;

    // Skip exactly what the histogram skipped, and scatter as many entries as it counted, or the
    // bucket bases and the scatter disagree.
    let visibility = gUniforms.visibility[globalIdx];
    let bits = visibility.visible & (cVisibleCurrentBit | cVisibleOutgoingBit);
    let entries = countbits(bits);
    let scatters = !instance.meshInstance.Null() && bucket < cMaxDrawBuckets && entries != 0u;
    let lane = visibility.Lane(bucket);

    // No early-out past this point: a thread that does not scatter must still reach every
    // GroupMemoryBarrierWithGroupSync below, or the ones that do scatter hang on it.
    uint groupSlot = 0u;
    if (scatters)
    {
        groupSlot = gGroupCount[lane].add(entries);
    }

    // The placement's impostor, one entry in the impostor bucket's lane, beside its submesh's.
    let impostor = !instance.meshInstance.Null() && (visibility.visible & cVisibleImpostorBit) != 0u;
    let impostorLane = visibility.Lane(cImpostorDrawBucket);
    uint impostorSlot = 0u;
    if (impostor)
    {
        impostorSlot = gGroupCount[impostorLane].add(1u);
    }
    GroupMemoryBarrierWithGroupSync();

    // One reservation per lane per group, and none at all for a lane this group did not fill.
    // Reserving per instance instead makes threadCountX a single contended address -- a scene drawn
    // from one bucket serializes its every instance on it.
    for (uint i = localIdx; i < cMaxDrawLanes; i += cCompactGroupSize)
    {
        let count = gGroupCount[i].load();
        gGroupBase[i] = (count != 0u) ? gUniforms.dispatchArgs[i].threadCountX.add(count) : 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    if (scatters)
    {
        let base = (lane == 0u) ? 0u : gUniforms.drawBucketPrefixSum[lane - 1u];
        var slot = base + gGroupBase[lane] + groupSlot;
        if ((bits & cVisibleCurrentBit) != 0u)
        {
            gUniforms.compactedInstances[slot] = globalIdx;
            ++slot;
        }
        if ((bits & cVisibleOutgoingBit) != 0u)
        {
            gUniforms.compactedInstances[slot] = globalIdx | cOutgoingDrawBit;
        }
    }
    if (impostor)
    {
        let base = (impostorLane == 0u) ? 0u : gUniforms.drawBucketPrefixSum[impostorLane - 1u];
        gUniforms.compactedInstances[base + gGroupBase[impostorLane] + impostorSlot] = globalIdx;
    }
}
