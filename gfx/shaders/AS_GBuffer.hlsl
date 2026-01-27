#include "common/Mesh.hlsli"

groupshared MeshletPayload s_payload;

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
    uint meshCount;
};

StructuredBuffer<uint> vertexMapBuffer : register(t0, space0);
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
StructuredBuffer<uint> indexBuffer : register(t2, space0);
StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);
StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0);
StructuredBuffer<Meshlet> meshletBuffer : register(t5, space0);


bool IsSphereInFrustum(float4 planes[6], float3 center, float radius)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
            return false;
    }
    return true;
}

bool IsPointInFrustum(float4 planes[6], float3 pt)
{
    [unroll]
    for (
int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, pt) + planes[i].w < 0)
            return false;
    }
    return true;
}

[numthreads(64, 1, 1)]
void AS_GBuffer(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint instanceID = gid;
    MeshInstance instance = instanceBuffer[instanceID];
    MeshInfo info = meshInfoBuffer[instance.infoID];

    if (gtid == 0)
    {
        s_payload.instanceID = instanceID;
        s_payload.visibleCount = 0;
        // optional: zero out entries if you want deterministic memory (not required)
        // for (uint k = 0; k < MAX_AS_MESHLETS; ++k) s_payload.visibleIndices[k] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    float4x4 vp = mul(projMatrix, viewMatrix);
    float4 planes[6];
    planes[0] = vp[3] + vp[0]; // left
    planes[1] = vp[3] - vp[0]; // right
    planes[2] = vp[3] + vp[1]; // top
    planes[3] = vp[3] - vp[1]; // bottom
    planes[4] = vp[2]; // near
    planes[5] = vp[3] - vp[2]; // far

    [unroll]
    for (int p = 0; p < 6; ++p)
    {
        planes[p] /= length(planes[p].xyz);
    }

    float3 scale;
    scale.x = length(instance.modelTransform[0].xyz);
    scale.y = length(instance.modelTransform[1].xyz);
    scale.z = length(instance.modelTransform[2].xyz);
    float maxScale = max(scale.x, max(scale.y, scale.z));

    for (uint i = gtid; i < info.meshletCount; i += 64)
    {
        uint meshletIndex = info.meshletBaseIndex + i;
        Meshlet m = meshletBuffer[meshletIndex];

        float4 centerWorld = mul(instance.modelTransform, float4(m.boundingCenter, 1.0f));
        float scaledRadius = m.boundingRadius * maxScale;

        // bool isVisible = IsPointInFrustum(planes, centerWorld.xyz);
        bool isVisible = IsSphereInFrustum(planes, centerWorld.xyz, scaledRadius);

        if (isVisible)
        {
            uint slot;
            InterlockedAdd(s_payload.visibleCount, 1, slot);

            if (slot < MAX_AS_MESHLETS)
            {
                s_payload.visibleIndices[slot] = meshletIndex;
            }
        }
    }

    uint dispatchCount = 0;
    if (gtid == 0)
    {
        dispatchCount = min(s_payload.visibleCount, (uint) MAX_AS_MESHLETS);
    }

    GroupMemoryBarrierWithGroupSync();

    MeshletPayload payload = s_payload;

    DispatchMesh(dispatchCount, 1, 1, payload);
}
