#include "common/Mesh_v2.hlsli"
#include "common/Material.hlsli"
#include "common/IndirectDraw.hlsli"


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


cbuffer IndirectDrawPushConstants : register(b0, space1)
{
    uint drawIndex;
}
StructuredBuffer<uint> visibleMeshletIndices : register(t0, space1);
StructuredBuffer<MeshletIndirectDrawArg> indirectDrawArgsBuffer : register(t1, space1);


[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer_v2(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[MAX_PRIMS_PER_MESHLET],
    out vertices MeshVertexOut verts[MAX_VERTICES_PER_MESHLET]
)
{

    uint visibleBufferOffset = indirectDrawArgsBuffer[drawIndex].visibleBufferOffset;
    uint globalMeshletID = visibleMeshletIndices[visibleBufferOffset + gid];


    Meshlet m = meshletBuffer[globalMeshletID];


    MeshInstance instance = instanceBuffer[0];

    uint physicalInfoID = meshInfoRedirectBuffer[instance.meshInfoID];
    MeshInfo info = meshInfoBuffer[physicalInfoID];

    SetMeshOutputCounts(m.vertexCount, m.triangleCount);

    if (gtid < m.vertexCount)
    {
        uint physicalVertexMapBase = vertexMapRedirectBuffer[info.vertexSegment];
        
        uint mapIndex = physicalVertexMapBase + m.localVertexOffset + gtid;
        
        uint logicalVertexID = vertexMapBuffer[mapIndex];

        uint physicalVertexIndex = vertexRedirectBuffer[logicalVertexID];
        
        Vertex v = vertexBuffer[physicalVertexIndex];

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
        uint physicalIndexBase = indexRedirectBuffer[info.indexSegment];
        
        uint physicalIndexLocation = physicalIndexBase + m.localIndexOffset + (gtid * 3);

        uint i0 = indexBuffer[physicalIndexLocation + 0];
        uint i1 = indexBuffer[physicalIndexLocation + 1];
        uint i2 = indexBuffer[physicalIndexLocation + 2];

        tris[gtid] = uint3(i0, i1, i2);
    }
}
