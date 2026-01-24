#include "common/Mesh.hlsli"

groupshared MeshletPayload s_payload;

StructuredBuffer<uint> vertexMapBuffer : register(t0, space0);
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
StructuredBuffer<uint> indexBuffer : register(t2, space0);
StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);
StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0);
StructuredBuffer<Meshlet> meshletBuffer : register(t5, space0);

[numthreads(64, 1, 1)]
void AS_GBuffer(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID)
{
    uint instanceID = gid;
    MeshInstance instance = instanceBuffer[instanceID];
    MeshInfo info = meshInfoBuffer[instance.infoID];

    if (gtid == 0)
    {
        s_payload.instanceID = instanceID;
        s_payload.meshletBaseIndex = info.meshletBaseIndex;
    }

    GroupMemoryBarrierWithGroupSync();

    uint dispatchCount = (gtid == 0) ? info.meshletCount : 0;
    
    DispatchMesh(dispatchCount, 1, 1, s_payload);
}
