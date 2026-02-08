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

StructuredBuffer<DrawInstanceAndSortKey> g_SrcKeys : register(t0, space1);
StructuredBuffer<uint> g_GroupOffsets : register(t1, space1);
RWStructuredBuffer<DrawInstanceAndSortKey> g_DstKeys : register(u0, space1);

groupshared uint l_LocalBuckets[256];

[numthreads(256, 1, 1)]
void CS_Scatter(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    if (GTid.x < 256)
    {
        l_LocalBuckets[GTid.x] = 0;
    }
    
    GroupMemoryBarrierWithGroupSync();

    uint elementIdx = DTid.x;
    
    bool isActive = elementIdx < instanceCount;
    DrawInstanceAndSortKey myKey;

    if (isActive)
    {
        if (bitShift == 0)
        {
            DrawInstance inst = instanceBuffer[elementIdx + 1];
            myKey.sortKey = inst.sortKey;
            myKey.instance = elementIdx + 1;
        }
        else
        {
            myKey = g_SrcKeys[elementIdx];
        }

        uint radix = (uint) ((myKey.sortKey >> bitShift) & 0xFF);

        uint localRank;
        InterlockedAdd(l_LocalBuckets[radix], 1, localRank);

        uint groupOffsetIdx = (Gid.x * 256) + radix;
        uint globalBaseOffset = g_GroupOffsets[groupOffsetIdx];

        uint finalDestinationIndex = globalBaseOffset + localRank;

        g_DstKeys[finalDestinationIndex] = myKey;
    }
}
