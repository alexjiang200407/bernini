import types.ComputeBuffer;
import idl.Constants;
import idl.DispatchArgs;

struct Uniforms
{
    // (sortKey, instanceIndex) pairs from TransparentDepthKeys, sorted in place.
    ComputeBuffer<uint2> entries;
    ComputeBuffer<uint> count;

    // The sorted instance indices alone, which is what the amplification shader indexes.
    ComputeBuffer<uint> sortedInstances;

    // Where each partition starts in sortedInstances, and its dispatch args. Both are indexed by
    // cOccludePartition / cPlainPartition.
    ComputeBuffer<uint> partitionBase;
    ComputeBuffer<DispatchArgs> partitionDispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint2 gItems[cTransparentSortCapacity];
groupshared uint gOccludeCount;

[shader("compute")]
[numthreads(cTransparentSortGroupSize, 1, 1)]
void main(uint tid: SV_GroupThreadID)
{
    const uint n = min(gUniforms.count[0], cTransparentSortCapacity);

    if (tid == 0u)
    {
        gOccludeCount = 0u;
    }

    // Pad to the full power of two with the reserved key so the padding sorts to the tail and never
    // displaces a real entry.
    for (uint i = tid; i < cTransparentSortCapacity; i += cTransparentSortGroupSize)
    {
        gItems[i] = (i < n) ? gUniforms.entries[i] : uint2(cSortPadKey, cSortPadKey);
    }
    GroupMemoryBarrierWithGroupSync();

    // Bitonic network. Each step partitions the array into disjoint (idx, idx ^ j) pairs, and only
    // the lower index of a pair writes, so the two elements a thread may own never overlap another
    // thread's pair -- which is what makes one barrier per step sufficient.
    for (uint k = 2u; k <= cTransparentSortCapacity; k <<= 1u)
    {
        for (uint j = k >> 1u; j > 0u; j >>= 1u)
        {
            for (uint idx = tid; idx < cTransparentSortCapacity; idx += cTransparentSortGroupSize)
            {
                const uint partner = idx ^ j;
                if (partner > idx)
                {
                    const bool ascending = (idx & k) == 0u;

                    const uint2 lhs = gItems[idx];
                    const uint2 rhs = gItems[partner];

                    if ((lhs.x > rhs.x) == ascending)
                    {
                        gItems[idx] = rhs;
                        gItems[partner] = lhs;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }

    // The depth-key pass puts the self-occluding instances in the low half of the key space, so the
    // sorted list is already split -- counting the low half is all the partitioning that is left.
    for (uint i = tid; i < n; i += cTransparentSortGroupSize)
    {
        gUniforms.entries[i] = gItems[i];
        gUniforms.sortedInstances[i] = gItems[i].y;

        if (gItems[i].x < 0x80000000u)
        {
            uint unused;
            InterlockedAdd(gOccludeCount, 1u, unused);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (tid == 0u)
    {
        const uint occludeCount = gOccludeCount;

        gUniforms.partitionBase[cOccludePartition] = 0u;
        gUniforms.partitionBase[cPlainPartition] = occludeCount;

        gUniforms.partitionDispatchArgs[cOccludePartition] = { occludeCount, 1u, 1u };
        gUniforms.partitionDispatchArgs[cPlainPartition] = { n - occludeCount, 1u, 1u };
    }
}
