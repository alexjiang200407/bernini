import bgl.MaterialReader;

/**
 * What the toon environment model reads off a surface: the material's half of shading an
 * environment, the engine's toon lighting the other. A model apart from the character's
 * (bgl.ToonCharacterSurface) because the two are different artists' work and grow apart: an
 * environment has one shade and no face.
 *
 * Construct one as `ToonEnvironmentSurface()` and set what the surface means to; each default is
 * what a surface that says nothing about the field means -- lit flat, as a step of zero is.
 */
public struct ToonEnvironmentSurface
{
    public float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);

    // Multiplies baseColor in the shade.
    public float3 shade = float3(0.75, 0.75, 0.75);

    // Below shadeStep on the half-Lambert term the shade takes over, its edge blending over
    // shadeFeather. Zero keeps every pixel lit.
    public float shadeStep = 0.0;
    public float shadeFeather = 0.0001;
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
