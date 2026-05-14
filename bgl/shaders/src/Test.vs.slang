import types.Vertex;
import bindings.SceneData;

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};

[[vk::push_constant]]
ConstantBuffer<SceneDataConstants> g_PushConstants;

[shader("vertex")]
VertexStageOutput main(uint vID : SV_VertexID)
{
    VertexStageOutput output;

    Vertex v = g_PushConstants.LoadVertex(vID);
    output.sv_position = float4(v.pos, 1.0);

    return output;
}
