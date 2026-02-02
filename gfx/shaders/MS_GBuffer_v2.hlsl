#include "common/Mesh_v2.hlsli"
#include "common/Material.hlsli"
#include "common/IndirectDraw.hlsli"

// ==========================================
// 1. INDIRECT ROOT CONSTANTS
// ==========================================
// This is updated by the Command Signature before dispatch.
// It tells us where in the 'visibleMeshletIndices' buffer this specific draw call starts.

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
};

// ==========================================
// 2. RESOURCE BINDINGS
// ==========================================

// --- SPACE 0: STATIC SCENE DATA ---
StructuredBuffer<uint> vertexMapBuffer : register(t0, space0);
StructuredBuffer<MeshInfo> meshInfoBuffer : register(t1, space0);
StructuredBuffer<uint> indexBuffer : register(t2, space0);
StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);
StructuredBuffer<MeshInstance> instanceBuffer : register(t4, space0);
StructuredBuffer<Meshlet> meshletBuffer : register(t5, space0);

// --- SPACE 0: REDIRECT TABLES (VIRTUALIZATION) ---
StructuredBuffer<uint> meshInfoRedirectBuffer : register(t6, space0);
StructuredBuffer<uint> vertexMapRedirectBuffer : register(t7, space0);
StructuredBuffer<uint> indexRedirectBuffer : register(t8, space0);
StructuredBuffer<uint> vertexRedirectBuffer : register(t9, space0);
StructuredBuffer<uint> meshletRedirectBuffer : register(t10, space0);

// --- SPACE 1: DYNAMIC PASS DATA ---
// We only need the list of visible indices. 
// The 'DrawArgs' are consumed by the hardware (Command Processor), not the shader.
cbuffer IndirectDrawPushConstants : register(b0, space1)
{
    uint drawIndex;
}
StructuredBuffer<uint> visibleMeshletIndices : register(t0, space1);
StructuredBuffer<MeshletIndirectDrawArg> indirectDrawArgsBuffer : register(t1, space1);

// ==========================================
// 3. MESH SHADER
// ==========================================

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer_v2(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[MAX_PRIMS_PER_MESHLET],
    out vertices MeshVertexOut verts[MAX_VERTICES_PER_MESHLET]
)
{
    // ---------------------------------------------------------
    // A. INDIRECT FETCH & VIRTUALIZATION
    // ---------------------------------------------------------
    
    // 1. Get the Global Meshlet ID from the visible list
    // The 'visibleBufferOffset' is pushed via Root Constant by ExecuteIndirect
    uint visibleBufferOffset = indirectDrawArgsBuffer[drawIndex].visibleBufferOffset;
    uint globalMeshletID = visibleMeshletIndices[visibleBufferOffset + gid];

    // 2. Fetch the Meshlet
    // Meshlets are stored in a single giant buffer, but accesses might need redirection 
    // depending on how your engine compacts them. 
    // Assuming 'globalMeshletID' is a direct index into the *Logical* buffer, 
    // we might need to redirect it if the Meshlet Buffer itself is segmented.
    // However, usually, the ID in the visible list is the absolute index.
    // IF meshlets are virtualized: 
    // You would need to know which "Segment" this ID belongs to. 
    // For now, assuming direct access or that the ID is already physical:
    Meshlet m = meshletBuffer[globalMeshletID];

    // 3. Get Instance Data
    MeshInstance instance = instanceBuffer[m.instanceID];
    
    // 4. Resolve MeshInfo (Redirected)
    // The instance points to a logical ID (handle). We map it to physical memory.
    uint physicalInfoID = meshInfoRedirectBuffer[instance.meshInfoID];
    MeshInfo info = meshInfoBuffer[physicalInfoID];

    SetMeshOutputCounts(m.vertexCount, m.triangleCount);

    // ---------------------------------------------------------
    // B. VERTEX PROCESSING
    // ---------------------------------------------------------
    if (gtid < m.vertexCount)
    {
        // 1. Get Physical Base of the Vertex Map
        // info.vertexSegment is a handle. Map it to a physical offset.
        uint physicalVertexMapBase = vertexMapRedirectBuffer[info.vertexSegment];
        
        // 2. Calculate the specific map entry location
        // Base + MeshletLocalOffset + ThreadID
        uint mapIndex = physicalVertexMapBase + m.localVertexOffset + gtid;
        
        // 3. Read the Global Vertex ID (Logical Handle)
        uint logicalVertexID = vertexMapBuffer[mapIndex];

        // 4. Resolve Physical Vertex Location
        // The vertex ID from the map is a handle. Map it to physical memory.
        uint physicalVertexIndex = vertexRedirectBuffer[logicalVertexID];
        
        // 5. Fetch Vertex
        Vertex v = vertexBuffer[physicalVertexIndex];

        // 6. Transform
        MeshVertexOut vOut;
        float4 worldPos = mul(instance.modelTransform, float4(v.position, 1.0));
        float4 viewPos = mul(viewMatrix, worldPos);
        vOut.position = mul(projMatrix, viewPos);
        vOut.normal = mul((float3x3) instance.modelTransform, v.normal);
        vOut.uv = v.uv;

        verts[gtid] = vOut;
    }

    // ---------------------------------------------------------
    // C. TRIANGLE PROCESSING
    // ---------------------------------------------------------
    if (gtid < m.triangleCount)
    {
        // 1. Get Physical Base of the Index Buffer
        // info.indexSegment is a handle. Map it to a physical offset.
        uint physicalIndexBase = indexRedirectBuffer[info.indexSegment];
        
        // 2. Calculate the specific index location
        // Base + MeshletLocalOffset + ThreadID * 3
        uint physicalIndexLocation = physicalIndexBase + m.localIndexOffset + (gtid * 3);

        // 3. Fetch Indices (Topology)
        uint i0 = indexBuffer[physicalIndexLocation + 0];
        uint i1 = indexBuffer[physicalIndexLocation + 1];
        uint i2 = indexBuffer[physicalIndexLocation + 2];

        tris[gtid] = uint3(i0, i1, i2);
    }
}
