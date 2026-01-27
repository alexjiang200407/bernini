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
    if (gid >= i_payload.visibleCount)
        return;

    MeshInstance instance = instanceBuffer[i_payload.instanceID];
    MeshInfo info = meshInfoBuffer[instance.infoID];
        
    uint meshletIndex = i_payload.visibleIndices[gid];
    Meshlet m = meshletBuffer[meshletIndex];

    SetMeshOutputCounts(m.vertexCount, m.triangleCount);

    if (gtid < m.vertexCount)
    {
        uint globalVertexIdx = vertexMapBuffer[m.vertexMapOffset + gtid];
        Vertex v = vertexBuffer[globalVertexIdx];

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
        uint indexLocation = m.indexOffset + (gtid * 3);

        uint i0 = indexBuffer[indexLocation];
        uint i1 = indexBuffer[indexLocation + 1];
        uint i2 = indexBuffer[indexLocation + 2];

        tris[gtid] = uint3(i0, i1, i2);
    }
}
