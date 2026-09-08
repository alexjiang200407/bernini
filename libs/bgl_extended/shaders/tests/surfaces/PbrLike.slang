import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

// A surface that fills PbrSurface exactly as the engine's own PBR record does, so a scene drawn
// through a reserved game slot lands on the engine's answer for that scene and not near it. The
// gate for the whole contract: if the two disagree, the seam between them is wrong.
struct PbrLikeParams
{
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    [Default(1.0)]
    float roughnessFactor;

    [Default(1.0)]
    float metallicFactor;

    ColorSlot baseColor;
    DataSlot orm;
    NormalSlot normal;
};

struct PbrLikeSurface : ISurfaceSource
{
    typealias MaterialParams = PbrLikeParams;

    static float Coverage<R : IMaterialReader>(R reader, PbrLikeParams params)
    {
        return reader.Sample(params.baseColor, reader.Uv()).a * params.baseColorFactor.a;
    }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, PbrLikeParams params)
    {
        let uv = reader.Uv();

        PbrSurface surface = PbrSurface();
        surface.baseColor = reader.Sample(params.baseColor, uv) * params.baseColorFactor;
        surface.orm = reader.Sample(params.orm, uv).rgb * float3(1.0, params.roughnessFactor, params.metallicFactor);
        surface.normalXY = reader.Sample(params.normal, uv).xy;
        return surface;
    }
};
