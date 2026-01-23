#define MAX_PRIMS_PER_MESHLET 1
#define MAX_VERTICES_PER_MESHLET 3

static const float2 g_positions[] =
{
    float2(-0.5, -0.5),
	float2(0, 0.5),
	float2(0.5, -0.5)
};

static const float3 g_colors[] =
{
    float3(1, 0, 0),
	float3(0, 1, 0),
	float3(0, 0, 1)
};

struct Payload
{
    int dummy;
};

struct Vertex
{
    float4 pos : SV_Position;
    float3 color : COLOR;
};


[NumThreads(3, 1, 1)]
[OutputTopology("triangle")]
void MS_GBuffer(
    uint threadId : SV_GroupThreadID,
    in payload Payload i_payload,
    out indices uint3 o_tris[MAX_PRIMS_PER_MESHLET],
    out vertices Vertex o_verts[MAX_VERTICES_PER_MESHLET]
)
{
    SetMeshOutputCounts(3, 1);
    
    o_tris[0] = uint3(0, 1, 2);

    for (uint i = 0; i < 3; i++)
    {
        o_verts[i].pos = float4(g_positions[i], 0, 1);
        o_verts[i].color = g_colors[i];
    }
}
