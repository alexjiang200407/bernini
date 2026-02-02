#ifndef MESH_V2_HLSLI
#define MESH_V2_HLSLI

struct MeshInstance
{
    uint meshInfoID;
    uint pad0[3];
    float4x4 modelTransform;
};

struct MeshInfo
{
    uint vertexSegment;
    uint indexSegment;
    uint meshletSegment;
    uint meshletCount;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct Meshlet
{
    uint localVertexOffset;
    uint vertexCount;
    
    uint localIndexOffset;
    uint triangleCount;

    uint materialID;
    uint instanceID;
    
    float3 boundingCenter;
    float boundingRadius;
};

#define MAX_PRIMS_PER_MESHLET 124
#define MAX_VERTICES_PER_MESHLET 64
#define MAX_AS_MESHLETS 64

struct MeshletPayload
{
    uint instanceID;
    uint meshletBaseIndex;
    uint meshletSegment;
    uint visibleCount;
    uint visibleIndices[MAX_AS_MESHLETS];
};

struct MeshVertexOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

#endif
