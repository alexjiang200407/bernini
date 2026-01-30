#include "common/Mesh.hlsli"

groupshared MeshletPayload s_payload;

StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0);
StructuredBuffer<uint> meshInfoRedirectBuffer : register(t6, space0);

[numthreads(64, 1, 1)]
void AS_GBuffer(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint physicalInstanceID = gid + 1;
    MeshInstance instance = instanceBuffer[physicalInstanceID];
    
    uint physicalMeshID = meshInfoRedirectBuffer[instance.infoID];
    MeshInfo info = meshInfoBuffer[physicalMeshID];

    if (gtid == 0)
    {
        s_payload.instanceID = physicalInstanceID;
        s_payload.meshletSegment = info.meshletSegment; // TODO: Map virtual meshlet ID
    }

    GroupMemoryBarrierWithGroupSync();

    uint dispatchCount = (gtid == 0) ? info.meshletCount : 0;
    
    DispatchMesh(dispatchCount, 1, 1, s_payload);
}
