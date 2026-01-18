#include "common/Mesh.hlsli"

StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
RWStructuredBuffer<DrawIndexedArgs> drawArgsBuffer : register(u0, space0);
RWStructuredBuffer<uint> drawIndirectCount         : register(u1, space0);

RWStructuredBuffer<uint> meshVisibleCount : register(u2, space0);
RWStructuredBuffer<uint> meshInstanceOffset : register(u3, space0);
RWStructuredBuffer<uint> meshWriteCursor              : register(u4, space0);

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
    uint meshCount;
};

[numthreads(1,1,1)]
void CS_BuildDrawArgs()
{
    uint drawCount = 0;

    for (uint meshID = 0; meshID < meshCount; meshID++)
    {
        uint count = meshWriteCursor[meshID];
        if (count == 0)
            continue;

        MeshInfo mesh = meshInfoBuffer[meshID];

        DrawIndexedArgs args;
        args.indexCountPerInstance = mesh.indexCount;
        args.instanceCount         = count;
        args.startIndexLocation    = mesh.startIndex;
        args.baseVertexLocation    = mesh.baseVertex;
        args.startInstanceLocation = meshInstanceOffset[meshID];

        drawArgsBuffer[drawCount++] = args;
    }

    drawIndirectCount[0] = drawCount;
}
