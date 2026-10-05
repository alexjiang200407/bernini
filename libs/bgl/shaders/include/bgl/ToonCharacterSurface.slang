import bgl.MaterialReader;

/**
 * What the toon character model reads off a surface: the material's half of shading a character,
 * the engine's toon lighting the other. A model apart from the environment's
 * (bgl.ToonEnvironmentSurface) because the two are different artists' work and grow apart.
 *
 * The cel model is three tones over the half-Lambert term `0.5 * dot(N, L) + 0.5`: lit, first
 * shade and second shade, each `baseColor` times its tint. The steps are values of that term; a
 * feather is the width the tone's edge blends over.
 *
 * Construct one as `ToonCharacterSurface()` and set what the surface means to; each default is
 * what a surface that says nothing about the field means.
 */
public struct ToonCharacterSurface
{
    public float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);

    // Multiply baseColor in the first and second shade.
    public float3 firstShade = float3(0.75, 0.75, 0.75);
    public float3 secondShade = float3(0.55, 0.55, 0.55);

    // Below baseStep the first shade takes over, and below shadeStep the second.
    public float baseStep = 0.5;
    public float baseFeather = 0.0001;
    public float shadeStep = 0.3;
    public float shadeFeather = 0.0001;

    // Added to the half-Lambert term before the steps: what a painted threshold map writes, to keep
    // an under-jaw in shade or a cheekbone lit whatever the light.
    public float shadeOffset = 0.0;

    // How much of this pixel is face, in [0, 1]: a face takes the remapped face light, the smoothed
    // normal and the edits of its placement's toon shading rig, the rest of the character the sun
    // on its own normal.
    public float face = 0.0;
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
