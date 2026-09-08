import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

// The second surface, so a scene can prove two slots coexist: no texture at all, one colour, and a
// coverage that comes from a parameter rather than a sample.
struct TintParams
{
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 tint;
};

struct TintSurface : ISurfaceSource
{
    typealias Params = TintParams;

    static float Coverage<R : IMaterialReader>(R reader, TintParams params) { return params.tint.a; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TintParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = params.tint;
        return surface;
    }
};
