import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.LitSurfaceSource;

// A lit surface that reads the light: toon bands quantized against the sun, the environment's
// irradiance as the ambient floor. What it pins is ISurfaceLight -- a sun moved by
// SetDirectionalLight moves the terminator, and no environment means bands on black.
struct BandParams
{
    [Color]
    [Default(1.0, 0.6, 0.2)]
    float3 litColor;

    [Color]
    [Default(0.1, 0.1, 0.3)]
    float3 shadowColor;

    [Default(3.0)]
    float bands;
};

struct BandSurface : ILitSurfaceSource
{
    typealias MaterialParams = BandParams;

    static float Coverage<R : IMaterialReader>(R reader, BandParams params) { return 1.0; }

    static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, BandParams params)
    {
        let facing = max(dot(reader.WorldNormal(), -light.SunDirection()), 0.0);
        let band = floor(facing * params.bands) / max(params.bands - 1.0, 1.0);
        let ramp = lerp(params.shadowColor, params.litColor, saturate(band));
        let ambient = light.Irradiance(reader.WorldNormal());
        return float4(ramp * max(light.SunRadiance(), ambient), 1.0);
    }
};
