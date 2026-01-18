#include "common/Mesh.hlsli"

StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0); 
RWStructuredBuffer<uint> meshVisibleCount : register(u2, space0);

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
    uint meshCount;
};

[numthreads(64,1,1)]
void CS_CullHistogram(uint3 tid : SV_DispatchThreadID)
{
    uint id = tid.x;
    if (id >= instanceCount)
        return;

    MeshInstance inst = instanceBuffer[id];

    float4x4 VP = mul(projMatrix, viewMatrix);
    float4 clip = mul(VP, mul(inst.modelTransform, float4(0,0,0,1)));

    bool visible =
        abs(clip.x) <= clip.w &&
        abs(clip.y) <= clip.w &&
        clip.z >= 0 &&
        clip.z <= clip.w;

    if (visible)
    {
        InterlockedAdd(meshVisibleCount[inst.infoID], 1);
    }
}
