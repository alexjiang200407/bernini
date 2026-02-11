#include "common/SortKey.hlsli"

RWStructuredBuffer<uint> g_PsoBinOffsets : register(u0, space1);

cbuffer Constants : register(b0, space1)
{
    uint instanceCount;
};

// Ensures g_PsoBinOffsets is monotonically non-decreasing.
// After FindPSOBoundaries, bins with zero instances may have stale (cleared-to-0) offsets.
// This pass propagates the previous bin's offset forward so that empty bins get start == end,
// and caps the final sentinel at instanceCount.
[numthreads(1, 1, 1)]
void CS_FixPSOBinOffsets(uint3 dtid : SV_DispatchThreadID)
{
    uint prev = 0;

    [unroll]
    for (uint i = 0; i <= MAX_PSO_BINS; ++i)
    {
        uint current = g_PsoBinOffsets[i];

        if (current < prev)
        {
            g_PsoBinOffsets[i] = prev;
        }
        else
        {
            prev = current;
        }
    }
}
