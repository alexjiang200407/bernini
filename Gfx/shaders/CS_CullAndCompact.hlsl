#include "common/Mesh.hlsli"

// SRVs
StructuredBuffer<MeshInstance> instanceBuffer : register(t0, space0);

// UAVs
RWStructuredBuffer<DrawIndexedArgs> drawArgsBuffer        : register(u0, space0); // unused here but bound
RWStructuredBuffer<uint>            drawIndirectCount     : register(u1, space0); // unused here but bound
RWStructuredBuffer<MeshInstance>    visibleInstanceBuffer : register(u2, space0);
RWStructuredBuffer<uint>            visibleInstanceCount  : register(u3, space0);

// CB
cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint     instanceCount;
};

[numthreads(64, 1, 1)]
void CS_CullAndCompact(uint3 DTid : SV_DispatchThreadID)
{
    uint instanceID = DTid.x;
    if (instanceID >= instanceCount)
        return;

    MeshInstance inst = instanceBuffer[instanceID];

    float4x4 VP   = mul(projMatrix, viewMatrix);
    float4 world  = mul(inst.modelTransform, float4(0, 0, 0, 1));
    float4 clip   = mul(VP, world);

    bool visible =
        abs(clip.x) <= clip.w &&
        abs(clip.y) <= clip.w &&
        clip.z >= 0.0 &&
        clip.z <= clip.w;

    if (!visible)
        return;

    uint writeIndex;
    InterlockedAdd(visibleInstanceCount[0], 1, writeIndex);

    visibleInstanceBuffer[writeIndex] = inst;
}
