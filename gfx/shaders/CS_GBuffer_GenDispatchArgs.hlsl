#include "common/IndirectDraw.hlsli"
#include "common/SortKey.hlsli"

StructuredBuffer<uint> g_BinMeshletCounts : register(t0, space2);
RWStructuredBuffer<MeshletDispatchArg> g_DrawArgs : register(u0, space2);

#define MESHLETS_PER_AS_GROUP 64

[numthreads(64, 1, 1)]
void CS_GBuffer_GenDispatchArgs(uint3 dtid : SV_DispatchThreadID)
{
    uint binIndex = dtid.x;
    
    if (binIndex >= MAX_PSO_BINS)
        return;
    
    uint meshletCount = g_BinMeshletCounts[binIndex];
    
    MeshletDispatchArg arg;
    
    if (meshletCount > 0)
    {
        uint threadGroupCount = (meshletCount + MESHLETS_PER_AS_GROUP - 1) / MESHLETS_PER_AS_GROUP;
        
        arg.threadGroupCountX = threadGroupCount;
        arg.threadGroupCountY = 1;
        arg.threadGroupCountZ = 1;
    }
    else
    {
        arg.threadGroupCountX = 0;
        arg.threadGroupCountY = 1;
        arg.threadGroupCountZ = 1;
    }
    
    g_DrawArgs[binIndex] = arg;
}
