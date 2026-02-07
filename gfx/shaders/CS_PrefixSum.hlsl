cbuffer ScanConstants : register(b0, space1)
{
    uint numGroups;
};
RWStructuredBuffer<uint> groupOffsets : register(u0, space1);

groupshared uint l_ScanData[256];

[numthreads(256, 1, 1)]
void CS_PrefixSum(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint bucketIdx = DTid.x;
    uint tid = GTid.x;

    uint myTotalCount = 0;
    
    for (uint i = 0; i < numGroups; ++i)
    {
        uint idx = (i * 256) + bucketIdx;
        myTotalCount += groupOffsets[idx];
    }

    l_ScanData[tid] = myTotalCount;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 1; stride < 256; stride *= 2)
    {
        uint index = (tid + 1) * (stride * 2) - 1;

        if (index < 256)
        {
            l_ScanData[index] += l_ScanData[index - stride];
        }
        
        GroupMemoryBarrierWithGroupSync();
    }
    
    if (tid == 0)
    {
        l_ScanData[255] = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    [unroll]
    for (uint stride2 = 128; stride2 > 0; stride2 /= 2)
    {
        uint index = (tid + 1) * (stride2 * 2) - 1;

        if (index < 256)
        {
            uint leftChildIdx = index - stride2;
            uint temp = l_ScanData[index];
            
            l_ScanData[index] += l_ScanData[leftChildIdx];
            l_ScanData[leftChildIdx] = temp;
        }
        
        GroupMemoryBarrierWithGroupSync();
    }
    
    uint globalOffset = l_ScanData[bucketIdx];
    uint currentRunningOffset = globalOffset;

    for (uint k = 0; k < numGroups; ++k)
    {
        uint idx = (k * 256) + bucketIdx;
        uint count = groupOffsets[idx];
        
        groupOffsets[idx] = currentRunningOffset;
        currentRunningOffset += count;
    }
}
