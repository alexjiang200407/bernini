import constants.TestConstants;

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};

[shader("pixel")]
float4 main(VertexStageOutput input) : SV_Target
{
    return float4(pushConstants.color, 1.0);
}
