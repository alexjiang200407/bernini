import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

// A PBR surface that says how much of the ground it covers: one colour, and a cover that comes from
// a parameter. What it pins is that PbrSurface::groundCover reaches the view's ground-cover texture,
// and from there the grass that follows its ground.
struct CoverParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 color;

    [Default(1.0)]
    float cover;
};

struct CoverSurface : ISurfaceSource
{
    typealias MaterialParams = CoverParams;

    static float Coverage<R : IMaterialReader>(R reader, CoverParams params) { return 1.0; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, CoverParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = params.color;
        surface.groundCover = params.cover;
        return surface;
    }
};
