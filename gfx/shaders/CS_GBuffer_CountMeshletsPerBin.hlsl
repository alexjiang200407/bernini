#include "common/IndirectDraw.hlsli"
#include "common/Mesh.hlsli"
#include "common/MeshUtil.hlsli"
#include "common/SortKey.hlsli"
#include "common/RadixSort.hlsli"

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

StructuredBuffer<DrawInstanceAndSortKey> g_SortedInstances : register(t0, space1);
RWStructuredBuffer<uint> g_BinMeshletCounts : register(u0, space2);

[numthreads(256, 1, 1)]
void CS_GBuffer_CountMeshletsPerBin(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= instanceCount)
        return;
    
    DrawInstance inst = g_DrawInstances[g_SortedInstances[dtid.x].instance];
    
    uint psoIdx = GetPSOIndex(inst.sortKey);
    
    uint geomType = GetGeometryType(inst.sortKey);
    
    if (geomType == GeomType_StaticMesh)
    {
        uint physicalInstanceID = g_StaticMeshInstanceRedirect[inst.dataIdx];
        StaticMeshInstance physicalInst = g_StaticInstances[physicalInstanceID];
        uint physicalInfoID = g_MeshInfoRedirect[physicalInst.meshInfoID];
        StaticMeshInfo info = g_MeshInfoBuffer[physicalInfoID];
        
        InterlockedAdd(g_BinMeshletCounts[psoIdx], info.meshletCount);
    }
}
