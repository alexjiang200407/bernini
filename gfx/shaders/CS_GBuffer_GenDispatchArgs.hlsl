#include "common/IndirectDraw.hlsli"
#include "common/SortKey.hlsli"
#include "common/Mesh.hlsli"

// Offsets into sorted instance buffer for each PSO bin. The last element is the total instance count.
StructuredBuffer<uint> g_BinInstanceOffsets : register(t1, space1);

RWStructuredBuffer<DispatchArg> g_DrawArgs : register(u0, space2);

[numthreads(64, 1, 1)]
void CS_GBuffer_GenDispatchArgs(uint3 dtid : SV_DispatchThreadID)
{
    uint binIndex = dtid.x;
    if (binIndex >= MAX_PSO_BINS)
        return;
    
    uint binInstanceCount = g_BinInstanceOffsets[binIndex + 1] - g_BinInstanceOffsets[binIndex];

    DispatchArg arg;
    arg.threadGroupCountY = 1;
    arg.threadGroupCountZ = 1;

    if (binInstanceCount > 0)
    {
        arg.threadGroupCountX = binInstanceCount;
    }
    else
    {
        arg.threadGroupCountX = 0;
    }
    
    g_DrawArgs[binIndex] = arg;
}
