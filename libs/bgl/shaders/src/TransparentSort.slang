import types.ComputeBuffer;

// One workgroup sorts the whole list in groupshared memory, so the sort is a single dispatch with no
// ping-pong buffers and no cross-group scan. That caps it at cSortCapacity entries; a multi-group
// radix sort is the scale-up once a view can hold more transparent instances than this.
static const uint cSortCapacity = 1024; // must stay a power of two
static const uint cSortGroupSize = 512; // one thread per compare-exchange pair

struct Uniforms
{
    // (sortKey, instanceIndex) pairs from TransparentDepthKeys, sorted in place.
    ComputeBuffer<uint2> entries;
    ComputeBuffer<uint> count;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint2 gItems[cSortCapacity];

[shader("compute")]
[numthreads(cSortGroupSize, 1, 1)]
void main(uint tid: SV_GroupThreadID)
{
    const uint n = min(gUniforms.count[0], cSortCapacity);

    // Pad to the full power of two with the maximum key so the padding sorts to the tail and never
    // displaces a real entry.
    for (uint i = tid; i < cSortCapacity; i += cSortGroupSize)
    {
        gItems[i] = (i < n) ? gUniforms.entries[i] : uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    GroupMemoryBarrierWithGroupSync();

    // Bitonic network. Each step partitions the array into disjoint (idx, idx ^ j) pairs, and only
    // the lower index of a pair writes, so the two elements a thread may own never overlap another
    // thread's pair -- which is what makes one barrier per step sufficient.
    for (uint k = 2u; k <= cSortCapacity; k <<= 1u)
    {
        for (uint j = k >> 1u; j > 0u; j >>= 1u)
        {
            for (uint idx = tid; idx < cSortCapacity; idx += cSortGroupSize)
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

    for (uint i = tid; i < n; i += cSortGroupSize)
    {
        gUniforms.entries[i] = gItems[i];
    }
}
