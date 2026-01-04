#include "common/Mesh.hlsli"

StructuredBuffer<MeshInstance> instanceBuffer : register(t0, space0);
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);

RWStructuredBuffer<DrawIndexedArgs> drawArgsBuffer : register(u0, space0);
RWStructuredBuffer<uint> drawArgsCounter : register(u1, space0);

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

[numthreads(64, 1, 1)]
void CS_DrawIndirect(uint3 DTid : SV_DispatchThreadID)
{
    uint instanceID = DTid.x;
    if (instanceID >= instanceCount)
        return;

    MeshInstance instance = instanceBuffer[instanceID];
    MeshInfo mesh = meshInfoBuffer[instance.infoID];

    float4x4 viewProj = mul(projMatrix, viewMatrix);
    float4 worldPos = mul(instance.modelTransform, float4(0, 0, 0, 1));
    float4 clipPos = mul(viewProj, worldPos);

    bool visible = abs(clipPos.x) <= clipPos.w &&
                   abs(clipPos.y) <= clipPos.w &&
                   clipPos.z >= 0 && clipPos.z <= clipPos.w;

    if (visible)
    {
        uint writeIndex;
        InterlockedAdd(drawArgsCounter[0], 1, writeIndex);

        DrawIndexedArgs args;
        args.indexCountPerInstance = mesh.indexCount;
        args.instanceCount = 1;
        args.startIndexLocation = mesh.startIndex;
        args.baseVertexLocation = mesh.baseVertex;
        args.startInstanceLocation = instanceID;

        drawArgsBuffer[writeIndex] = args;
    }
}
