#include "common/Mesh.hlsli"

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

StructuredBuffer<uint> vertexMapBuffer : register(t0, space0);
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
StructuredBuffer<uint> indexBuffer : register(t2, space0);
StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);
StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0);
StructuredBuffer<Meshlet> meshletBuffer : register(t5, space0);

StructuredBuffer<uint> meshInfoRedirectBuffer : register(t6, space0);
StructuredBuffer<uint> vertexMapRedirectBuffer : register(t7, space0);
StructuredBuffer<uint> indexRedirectBuffer : register(t8, space0);
StructuredBuffer<uint> vertexRedirectBuffer : register(t9, space0);
StructuredBuffer<uint> meshletRedirectBuffer : register(t10, space0);

[NumThreads(64, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload MeshletPayload i_payload,
    out indices uint3 tris[MAX_PRIMS_PER_MESHLET],
    out vertices MeshVertexOut verts[MAX_VERTICES_PER_MESHLET]
)
{
    MeshInstance instance = instanceBuffer[i_payload.instanceID];
    
    // Use redirect buffer to get physical mesh info index
    uint physicalInfoID = meshInfoRedirectBuffer[instance.infoID];
    MeshInfo info = meshInfoBuffer[physicalInfoID];
    
    // Use redirect buffer to get physical base for meshlet segment, then add group offset
    uint physicalMeshletBase = meshletRedirectBuffer[info.meshletSegment];
    uint physicalMeshletIndex = physicalMeshletBase + gid;
    Meshlet m = meshletBuffer[physicalMeshletIndex];
    
    SetMeshOutputCounts(m.vertexCount, m.triangleCount);
    
    if (gtid < m.vertexCount)
    {
        // Use redirect buffer to get physical base for vertex map segment, then add thread offset
        uint physicalVertexMapBase = vertexMapRedirectBuffer[m.vertexMapSegment];
        uint physicalVertexMapIdx = physicalVertexMapBase + gtid;
        uint globalVertexIdx = vertexMapBuffer[physicalVertexMapIdx];
        
        // Use redirect buffer to get physical vertex index
        uint physicalVertexIdx = vertexRedirectBuffer[globalVertexIdx];
        Vertex v = vertexBuffer[physicalVertexIdx];
        
        MeshVertexOut vOut;
        float4 worldPos = mul(instance.modelTransform, float4(v.position, 1.0));
        float4 viewPos = mul(viewMatrix, worldPos);
        vOut.position = mul(projMatrix, viewPos);
        vOut.normal = mul((float3x3) instance.modelTransform, v.normal);
        vOut.uv = v.uv;
        verts[gtid] = vOut;
    }
    
    if (gtid < m.triangleCount)
    {
        // Use redirect buffer to get physical base for index segment, then add triangle offset
        uint physicalIndexBase = indexRedirectBuffer[m.indexSegment];
        uint physicalIndexLocation = physicalIndexBase + (gtid * 3);
        
        uint i0 = indexBuffer[physicalIndexLocation];
        uint i1 = indexBuffer[physicalIndexLocation + 1];
        uint i2 = indexBuffer[physicalIndexLocation + 2];
        
        tris[gtid] = uint3(i0, i1, i2);
    }
}
