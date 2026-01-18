#ifndef MESH_HLSLI
#define MESH_HLSLI

struct MeshInstance
{
    uint infoID;
    float4x4 modelTransform;
};

struct MeshInfo
{
    uint startIndex;
    uint indexCount;
    uint baseVertex;
    uint materialID;
};

struct DrawIndexedArgs
{
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 texCoord;
};


#endif
