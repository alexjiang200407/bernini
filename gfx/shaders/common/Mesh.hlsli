#ifndef MESH_HLSLI
#define MESH_HLSLI

struct MeshInstance
{
    uint infoID;
    uint pad0[3];
    float4x4 modelTransform;
};

struct MeshInfo
{
    uint meshletSegment;
    uint meshletCount;
    uint materialID;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct MeshVertexOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};


struct Meshlet
{
    uint vertexMapSegment;
    uint vertexCount;
    uint indexSegment;
    uint indexCount;
    uint triangleCount;
    float3 boundingCenter;
    float boundingRadius;
};

#define MAX_PRIMS_PER_MESHLET 124
#define MAX_VERTICES_PER_MESHLET 64

struct MeshletPayload
{
    uint instanceID;
    uint meshletSegment;
};

#endif
