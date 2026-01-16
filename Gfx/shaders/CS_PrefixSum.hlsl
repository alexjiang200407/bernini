RWStructuredBuffer<uint> meshVisibleCount   : register(u2, space0);
RWStructuredBuffer<uint> meshInstanceOffset : register(u3, space0);
RWStructuredBuffer<uint> meshWriteCursor    : register(u4, space0);

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint     instanceCount;
    uint     meshCount;
};

[numthreads(1,1,1)]
void CS_PrefixSum()
{
    uint sum = 0;

    for (uint i = 0; i < meshCount; i++)
    {
        meshInstanceOffset[i] = sum;
        meshWriteCursor[i]    = sum;
        sum += meshVisibleCount[i];
    }
}
