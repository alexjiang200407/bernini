#include "common/Mesh.hlsli"
#include "common/FrameDataBindings.hlsli"
#include "common/RadixSort.hlsli"

groupshared MeshletPayload s_payload;

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
    for (int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, pt) + planes[i].w < 0)
            return false;
    }
    return true;
}

StructuredBuffer<DrawInstanceAndSortKey> g_SortedKeys : register(t0, space1);
StructuredBuffer<uint> g_BinInstanceOffsets : register(t1, space1);

cbuffer RootConstants : register(b0, space2)
{
    uint binIndex;
};

[numthreads(64, 1, 1)]
void AS_GBuffer_StaticMesh(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint sortedKeysIdx = g_BinInstanceOffsets[binIndex] + gid;
    uint drawInstanceIdx = g_SortedKeys[sortedKeysIdx].instance;
    DrawInstance drawInst = g_DrawInstances[drawInstanceIdx];
    uint physicalInstanceIdx = g_StaticMeshInstanceRedirect[drawInst.specId];
    StaticMeshInstance instance = g_StaticInstances[physicalInstanceIdx];
    
    uint physicalMeshInfoID = g_MeshInfoRedirect[instance.meshInfoID];
    StaticMeshInfo info = g_MeshInfoBuffer[physicalMeshInfoID];

    if (gtid == 0)
    {
        s_payload.instanceIdx = physicalInstanceIdx;
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
        uint physicalMeshletBase = g_MeshletRedirect[info.meshletSegment];
        uint meshletIndex = physicalMeshletBase + i;
        Meshlet m = g_MeshletBuffer[meshletIndex];

        float4 centerWorld = mul(instance.modelTransform, float4(m.boundingCenter, 1.0f));
        float scaledRadius = m.boundingRadius * maxScale;

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
