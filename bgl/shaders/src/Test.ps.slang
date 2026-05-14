struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};

[shader("pixel")]
float4 main(VertexStageOutput input) : SV_Target
{
    // Just returning a solid white color
    return float4(1.0, 1.0, 1.0, 1.0);
}
