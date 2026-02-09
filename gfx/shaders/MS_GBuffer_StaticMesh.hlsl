#include "common/Material.hlsli"
#include "common/IndirectDraw.hlsli"
#include "common/FrameDataBindings.hlsli"

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer_StaticMesh(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload MeshletPayload payload,
    out indices uint3 tris[MAX_PRIMS_PER_MESHLET],
    out vertices MeshVertexOut verts[MAX_VERTICES_PER_MESHLET]
)
{
    if (gid >= payload.visibleCount)
        return;

    uint globalMeshletID = payload.visibleIndices[gid];
    Meshlet m = g_MeshletBuffer[globalMeshletID];

    StaticMeshInstance instance = g_StaticInstances[payload.instanceIdx];
    
    uint physicalInfoIdx = g_MeshInfoRedirect[instance.meshInfoID];
    StaticMeshInfo info = g_MeshInfoBuffer[physicalInfoIdx];
    
    SetMeshOutputCounts(m.vertexCount, m.triangleCount);

    if (gtid < m.vertexCount)
    {
        uint physicalVertexMapBase = g_VertexMapRedirect[info.vertexMapSegment];
        uint physicalVertexMapIdx = physicalVertexMapBase + m.localVertexOffset + gtid;
        uint globalVertexIdx = g_VertexMapBuffer[physicalVertexMapIdx];
        
        // Use redirect buffer to get physical vertex index
        uint physicalVertexIdx = g_VertexRedirect[globalVertexIdx];
        Vertex v = g_VertexBuffer[physicalVertexIdx];
        
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
        uint physicalIndexBase = g_IndexRedirect[info.indexSegment];
        uint physicalIndexLocation = physicalIndexBase + m.localIndexOffset + (gtid * 3);
        
        uint i0 = g_IndexBuffer[physicalIndexLocation];
        uint i1 = g_IndexBuffer[physicalIndexLocation + 1];
        uint i2 = g_IndexBuffer[physicalIndexLocation + 2];
        
        tris[gtid] = uint3(i0, i1, i2);
    }
}
