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
    typealias MaterialParams = TintParams;

    // Coverage is only read on a tested layer, and it is the one thing a surface answers with a
    // number rather than a sample -- so this varies across the mesh without a texture to vary by,
    // which is what makes a cutout visible at all.
    static float Coverage<R : IMaterialReader>(R reader, TintParams params) { return params.tint.a * step(0.5, frac(reader.Uv().x * 8.0)); }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TintParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = params.tint;
        return surface;
    }
};
