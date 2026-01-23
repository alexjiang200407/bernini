//#include "common/Mesh.hlsli"

//// Same bindings as other GBuffer shaders
//StructuredBuffer<uint> indexBuffer : register(t2, space0);
//StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);
//StructuredBuffer<MeshInstance> visibleInstanceBuffer : register(t5, space0);

//cbuffer FrameConstants : register(b0, space0)
//{
//    float4x4 viewMatrix;
//    float4x4 projMatrix;
//    uint instanceCount;
//};

//// Mock meshlet constants (replace with actual meshlet buffer when available)
//#define MAX_VERTICES 64
//#define MAX_PRIMITIVES 126

struct MSOutput
{
    float4 position : SV_POSITION;
};

[NumThreads(3, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer(
    uint gtid : SV_GroupThreadID,
    out vertices MSOutput verts[3],
    out indices uint3 tris[1]
)
{
    SetMeshOutputCounts(3, 1);

    float3 positions[3] =
    {
        float3(-0.5f, -0.5f, 0.0f),
        float3(0.5f, -0.5f, 0.0f),
        float3(0.0f, 0.5f, 0.0f)
    };

    verts[gtid].position = float4(positions[gtid], 1.0f);

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}
