import types.Vertex;
import constants.TestConstants;

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};


[shader("vertex")]
VertexStageOutput main(uint vID : SV_VertexID)
{
    VertexStageOutput output;

    Vertex v = pushConstants.vertexBuffer[vID];
    output.sv_position = float4(v.pos, 1.0);

    return output;
}
