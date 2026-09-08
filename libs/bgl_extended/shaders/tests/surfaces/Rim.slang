import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

// The proving surface: the engine's PBR model over a base colour, plus a view-dependent emissive
// the engine has no parameter for and no reason to grow one. Written the way a game writes one --
// it imports the contract and nothing else, and names no handle, arena or bucket.
struct RimParams
{
    [Default(0.2, 0.6, 1.0)]
    float3 rimColor;

    [Default(3.0)]
    float rimPower;

    ColorSlot baseColor;
};

struct RimSurface : ISurfaceSource
{
    typealias MaterialParams = RimParams;

    static float Coverage<R : IMaterialReader>(R reader, RimParams params) { return reader.Sample(params.baseColor, reader.Uv()).a; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, RimParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = reader.Sample(params.baseColor, reader.Uv());

        let n = normalize(reader.WorldNormal());
        let v = normalize(reader.CameraPos() - reader.WorldPos());
        surface.emissive = params.rimColor * pow(1.0 - saturate(dot(n, v)), params.rimPower);

        return surface;
    }
};
