import gbuffer.common;

[shader("pixel")]
float4 PSMain(GBufferVSOut input) : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}
