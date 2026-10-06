import bgl.MaterialReader;

/**
 * What the toon environment model reads off a surface: the material's half of shading an
 * environment, the engine's toon lighting the other. A model apart from the character's
 * (bgl.ToonCharacterSurface) because the two are different artists' work and grow apart; today the
 * lighting draws baseColor flat, until the environment's look is designed.
 *
 * Construct one as `ToonEnvironmentSurface()` and set what the surface means to; each default is
 * what a surface that says nothing about the field means.
 */
public struct ToonEnvironmentSurface
{
    public float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
};

/**
 * What an environment surface written outside the engine conforms to. The parameter struct and
 * Coverage follow ISurfaceSource's rules exactly; Evaluate returns the environment model's half,
 * and the engine lights it.
 */
public interface IToonEnvironmentSurfaceSource
{
    /// Everything a material sets on this surface, under the rules ISurfaceSource states.
    associatedtype MaterialParams;

    static float Coverage<R : IMaterialReader>(R reader, MaterialParams params);
    static ToonEnvironmentSurface Evaluate<R : IMaterialReader>(R reader, MaterialParams params);
};
