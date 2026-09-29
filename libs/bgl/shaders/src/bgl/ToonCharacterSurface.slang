import bgl.MaterialReader;

/**
 * What the toon character model reads off a surface: the material's half of shading a character,
 * the engine's toon lighting the other. A model apart from the environment's
 * (bgl.ToonEnvironmentSurface) because the two are different artists' work and grow apart; today
 * the lighting draws baseColor flat.
 *
 * Construct one as `ToonCharacterSurface()` and set what the surface means to; each default is
 * what a surface that says nothing about the field means.
 */
public struct ToonCharacterSurface
{
    public float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
};

/**
 * What a character surface written outside the engine conforms to. The parameter struct and
 * Coverage follow ISurfaceSource's rules exactly; Evaluate returns the character model's half, and
 * the engine lights it.
 */
public interface IToonCharacterSurfaceSource
{
    /// Everything a material sets on this surface, under the rules ISurfaceSource states.
    associatedtype MaterialParams;

    static float Coverage<R : IMaterialReader>(R reader, MaterialParams params);
    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, MaterialParams params);
};
