import idl.Constants;
import idl.DispatchArgs;
import lib.types.ComputeBuffer;

struct Uniforms
{
    // (sortKey, instanceIndex) pairs from TransparentDepthKeys, sorted in place.
    ComputeBuffer<uint2> entries;
    ComputeBuffer<uint> count;

    // The sorted instance indices alone, which is what the amplification shader indexes.
    ComputeBuffer<uint> sortedInstances;

    // The whole sorted list is one dispatch.
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint2 gItems[cTransparentSortCapacity];

[shader("compute")]
[numthreads(cTransparentSortGroupSize, 1, 1)]
void main(uint tid: SV_GroupThreadID)
{
    const uint n = min(gUniforms.count[0], cTransparentSortCapacity);

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

    for (uint i = tid; i < n; i += cTransparentSortGroupSize)
    {
        gUniforms.entries[i] = gItems[i];
        gUniforms.sortedInstances[i] = gItems[i].y;
    }
    GroupMemoryBarrierWithGroupSync();

    if (tid == 0u)
    {
        // Field-wise, not an aggregate: threadCountX is an Atomic<uint>, so it is written through
        // .store rather than positional initialization.
        gUniforms.dispatchArgs[0].threadCountX.store(n);
        gUniforms.dispatchArgs[0].threadCountY = 1u;
        gUniforms.dispatchArgs[0].threadCountZ = 1u;
    }
}
