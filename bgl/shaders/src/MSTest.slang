import types.Vertex;
import constants.TestConstants;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main(
    OutputVertices<VertexOutput, 3> verts,
    OutputIndices<uint3, 1>         tris)
{
    SetMeshOutputCounts(3, 1);

    Vertex v0 = gPushConstants.vertexBuffer[0];
    Vertex v1 = gPushConstants.vertexBuffer[1];
    Vertex v2 = gPushConstants.vertexBuffer[2];

    verts[0].position = float4(v0.pos, 1.0);
    verts[0].uv = float2(0.0, 0.0);
    verts[1].position = float4(v1.pos, 1.0);
    verts[1].uv = float2(1.0, 0.0);
    verts[2].position = float4(v2.pos, 1.0);
    verts[2].uv = float2(0.0, 1.0);

    tris[0] = uint3(0, 1, 2);
}