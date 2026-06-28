import forward.common;

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
