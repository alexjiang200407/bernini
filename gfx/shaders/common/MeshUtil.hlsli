#ifndef MESH_UTIL_HLSLI
#define MESH_UTIL_HLSLI

#include "Mesh.hlsli"
#include "IndirectDraw.hlsli"

StructuredBuffer<uint> g_VertexMapBuffer : register(t0);
StructuredBuffer<StaticMeshInfo> g_MeshInfoBuffer : register(t1);
StructuredBuffer<StaticMeshInstance> g_StaticInstances : register(t2);
StructuredBuffer<uint> g_IndexBuffer : register(t3);
StructuredBuffer<Vertex> g_VertexBuffer : register(t4);
StructuredBuffer<DrawInstance> g_DrawInstances : register(t5);
StructuredBuffer<Meshlet> g_MeshletBuffer : register(t6);

StructuredBuffer<uint> g_MeshInfoRedirect : register(t7);
StructuredBuffer<uint> g_VertexMapRedirect : register(t8);
StructuredBuffer<uint> g_IndexRedirect : register(t9);
StructuredBuffer<uint> g_VertexRedirect : register(t10);
StructuredBuffer<uint> g_MeshletRedirect : register(t11);
StructuredBuffer<uint> g_StaticMeshInstanceRedirect : register(t12);


struct StaticMeshData
{
    uint vertexSegment;
    uint indexSegment;
    uint meshletSegment;
    uint meshletCount;
    float4x4 modelTransform;
};

#endif
