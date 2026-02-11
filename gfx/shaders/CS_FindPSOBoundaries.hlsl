#include "common/SortKey.hlsli"
#include "common/RadixSort.hlsli"

StructuredBuffer<DrawInstanceAndSortKey> g_SortedKeys : register(t0, space1);
RWStructuredBuffer<uint> g_PsoBinOffsets : register(u0, space1);

cbuffer Constants : register(b0, space1)
{
    uint instanceCount;
};

[numthreads(256, 1, 1)]
void CS_FindPSOBoundaries(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    
    // We process up to instanceCount (inclusive) to allow a "virtual" 
    // thread to close the very last bin.
    if (idx > instanceCount)
        return;

    // Determine PSO for this index and the previous one
    // We use a sentinel value (0xFFFFFFFF) for indices out of range
    uint myPSO = (idx < instanceCount) ? GetPSOIndex(g_SortedKeys[idx].sortKey) : 0xFFFFFFFF;
    uint prevPSO = (idx > 0) ? GetPSOIndex(g_SortedKeys[idx - 1].sortKey) : 0xFFFFFFFF;

    // A transition occurs if the PSO index changes
    if (myPSO != prevPSO)
    {
        // 1. If we are at the start of the entire list
        if (idx == 0)
        {
            g_PsoBinOffsets[myPSO] = 0;
        }
        // 2. If we are at the end of the entire list (idx == instanceCount)
        else if (idx == instanceCount)
        {
            // prevPSO is the PSO of the very last item in the buffer
            g_PsoBinOffsets[prevPSO + 1] = instanceCount;
        }
        // 3. We are at a transition point between two different PSOs
        else
        {
            // The previous PSO ends here
            g_PsoBinOffsets[prevPSO + 1] = idx;
            // The new PSO starts here
            g_PsoBinOffsets[myPSO] = idx;
        }
    }
}
