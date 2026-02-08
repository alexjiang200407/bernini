#include "common/Mesh.hlsli"
#include "common/IndirectDraw.hlsli"
#include "common/RadixSort.hlsli"

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

StructuredBuffer<DrawInstance> instanceBuffer : register(t5, space0);

cbuffer SortConstants : register(b0, space1)
{
    uint bitShift;
};

RWStructuredBuffer<uint> groupOffsets : register(u0, space1);
StructuredBuffer<DrawInstanceAndSortKey> g_srcKeys : register(t0, space1);

groupshared uint localBuckets[256];

[numthreads(256, 1, 1)]
void CS_Histogram(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    if (GTid.x < 256)
        localBuckets[GTid.x] = 0;
    
    GroupMemoryBarrierWithGroupSync();
    
    uint globalKeyIdx = DTid.x;
    
    if (globalKeyIdx < instanceCount)
    {
        uint64_t sortKey = 0;
        if (bitShift == 0)
        {
            sortKey = instanceBuffer[globalKeyIdx + 1].sortKey;
        }
        else
        {
            sortKey = g_srcKeys[globalKeyIdx].sortKey;
        }
        uint radix = (uint) ((sortKey >> bitShift) & 0xFF);
        InterlockedAdd(localBuckets[radix], 1);
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (GTid.x < 256)
    {
        uint outputIdx = (Gid.x * 256) + GTid.x;
        groupOffsets[outputIdx] = localBuckets[GTid.x];
    }
}
