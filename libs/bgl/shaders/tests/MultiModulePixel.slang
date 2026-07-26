// A pixel entry alone in its own module, to prove the WGSL link joins a vertex stage from one
// module with a pixel stage from another -- the shape the forward shaders have, where the geometry
// and pixel stages live in separate sources.

[shader("pixel")]
float4 PSMain() : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
