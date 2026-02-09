#ifndef MESH_UTIL_HLSLI
#define MESH_UTIL_HLSLI

#include "Mesh.hlsli"
#include "IndirectDraw.hlsli"

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

StructuredBuffer<uint> g_VertexMapBuffer : register(t0);
StructuredBuffer<StaticMeshInfo> g_MeshInfoBuffer : register(t1);
StructuredBuffer<StaticMeshInstance> g_StaticInstances : register(t2);
StructuredBuffer<uint> g_IndexBuffer : register(t3);
StructuredBuffer<Vertex> g_VertexBuffer : register(t4);
StructuredBuffer<DrawInstance> g_DrawInstances : register(t5);
StructuredBuffer<Meshlet> g_MeshletBuffer : register(t6);

// Buffers for extracting the physical index from an id
StructuredBuffer<uint> g_MeshInfoRedirect : register(t7);
StructuredBuffer<uint> g_VertexMapRedirect : register(t8);
StructuredBuffer<uint> g_IndexRedirect : register(t9);
StructuredBuffer<uint> g_VertexRedirect : register(t10);
StructuredBuffer<uint> g_MeshletRedirect : register(t11);
StructuredBuffer<uint> g_StaticMeshInstanceRedirect : register(t12);


struct StaticMeshData
{
    uint instanceId;
    uint infoId;
    uint instanceIdx;
    uint infoIdx;
    uint vertexSegment;
    uint indexSegment;
    uint meshletSegment;
    uint meshletBaseIdx;
    uint meshletCount;
    float4x4 modelTransform;
};

StaticMeshData GetStaticMeshData(DrawInstance drawInstance)
{
    StaticMeshData data;

    uint instanceIdx = g_StaticMeshInstanceRedirect[drawInstance.specId];
    StaticMeshInstance instance = g_StaticInstances[instanceIdx];

    uint infoIdx = g_MeshInfoRedirect[instance.meshInfoID];
    StaticMeshInfo info = g_MeshInfoBuffer[infoIdx];

    data.instanceId = drawInstance.specId;
    data.infoId = instance.meshInfoID;
    data.instanceIdx = instanceIdx;
    data.infoIdx = infoIdx;
    data.vertexSegment = info.vertexSegment;
    data.indexSegment = info.indexSegment;
    data.meshletSegment = info.meshletSegment;
    data.meshletCount = info.meshletCount;
    data.modelTransform = instance.modelTransform;
    data.meshletBaseIdx = g_MeshletRedirect[info.meshletSegment];

    return data;
}

#endif
