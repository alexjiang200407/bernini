#include "common/Mesh.hlsli"

// SRVs
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);

// UAVs
RWStructuredBuffer<DrawIndexedArgs> drawArgsBuffer     : register(u0, space0);
RWStructuredBuffer<uint>            drawIndirectCount  : register(u1, space0);
RWStructuredBuffer<uint>            visibleInstanceCount : register(u3, space0);

[numthreads(1, 1, 1)]
void CS_BuildDrawArgs()
{
    uint visibleCount = visibleInstanceCount[0];

    if (visibleCount == 0)
    {
        drawIndirectCount[0] = 0;
        return;
    }

    MeshInfo mesh = meshInfoBuffer[0];

    DrawIndexedArgs args;
    args.indexCountPerInstance = mesh.indexCount;
    args.instanceCount         = visibleCount;
    args.startIndexLocation    = mesh.startIndex;
    args.baseVertexLocation    = mesh.baseVertex;
    args.startInstanceLocation = 0;

    drawArgsBuffer[0] = args;
    drawIndirectCount[0] = 1;
}
