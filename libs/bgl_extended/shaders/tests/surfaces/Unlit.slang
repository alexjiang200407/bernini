import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.LitSurfaceSource;

// The simplest surface on the lit contract: a constant, reading nothing of the light. What it
// pins is that Shade's answer reaches the screen unshaded -- no BRDF, no environment -- with only
// the exposure the engine applies between.
struct UnlitParams
{
    [Color]
    [Default(1.0, 1.0, 1.0)]
    float3 color;

    [Default(1.0)]
    float opacity;
};

struct UnlitSurface : ILitSurfaceSource
{
    typealias MaterialParams = UnlitParams;

    static float Coverage<R : IMaterialReader>(R reader, UnlitParams params) { return params.opacity; }

    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, UnlitParams params)
    {
        return float4(params.color, params.opacity);
    }
};
