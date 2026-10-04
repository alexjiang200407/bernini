import bgl.LitSurfaceSource;
import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.ToonCharacterSurface;
import bgl.ToonEnvironmentSurface;

/**
 * The toon character model: three tones over the half-Lambert term, each the base colour times its
 * tint, times the sun's radiance. Steps of zero keep every pixel lit. Alpha is the base colour's,
 * the coverage the blended layer reads.
 *
 * @param normal World space, unit, facing the viewer.
 */
public float4 ShadeToonCharacter<L : ISurfaceLight>(ToonCharacterSurface surface, L light, float3 normal)
{
    let toLight = -light.SunDirection();
    let halfLambert = 0.5 * dot(normal, toLight) + 0.5 + surface.shadeOffset;

    let intoShade = saturate((surface.baseStep - halfLambert) / max(surface.baseFeather, 1e-5));
    let intoSecond = saturate((surface.shadeStep - halfLambert) / max(surface.shadeFeather, 1e-5));

    let shade = lerp(surface.firstShade, surface.secondShade, intoSecond);
    let tone = lerp(float3(1.0), shade, intoShade);
    return float4(surface.baseColor.rgb * tone * light.SunRadiance(), surface.baseColor.a);
}

// The toon environment model, flat until its look is designed: the base colour as pre-exposure
// radiance, reading nothing of the light.
public float4 ShadeToonEnvironment<L : ISurfaceLight>(ToonEnvironmentSurface surface, L light)
{
    return surface.baseColor;
}

/**
 * A character surface as a lit one, its lighting the engine's toon character model. What a
 * registered character surface is bound as, so it draws through the lit programs -- the record,
 * the reader, every layer and grass -- and the model adds no program family of its own.
 */
public struct ToonCharacterLit<G : IToonCharacterSurfaceSource> : ILitSurfaceSource
{
    public typealias MaterialParams = G.MaterialParams;

    public static float Coverage<R : IMaterialReader>(R reader, MaterialParams params) { return G.Coverage(reader, params); }

    public static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params)
    {
        return ShadeToonCharacter(G.Evaluate(reader, params), light, normalize(reader.WorldNormal()));
    }
};

/// ToonCharacterLit's environment twin.
public struct ToonEnvironmentLit<G : IToonEnvironmentSurfaceSource> : ILitSurfaceSource
{
    public typealias MaterialParams = G.MaterialParams;

    public static float Coverage<R : IMaterialReader>(R reader, MaterialParams params) { return G.Coverage(reader, params); }

    public static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params)
    {
        return ShadeToonEnvironment(G.Evaluate(reader, params), light);
    }
};
