import bgl.LitSurfaceSource;
import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.ToonCharacterSurface;
import bgl.ToonEnvironmentSurface;

// The toon models' lighting, one per model. Flat until the look is designed: the base colour as
// pre-exposure radiance, reading nothing of the light. Alpha is the base colour's, the coverage the
// blended layer reads.
public float4 ShadeToonCharacter<L : ISurfaceLight>(ToonCharacterSurface surface, L light)
{
    return surface.baseColor;
}

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
        return ShadeToonCharacter(G.Evaluate(reader, params), light);
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
