/**
 * What a surface that owns its lighting is lit by: the scene's one sun and the environment, every
 * dereference already done on the engine's side. Directions are world space; the environment's
 * rotation is applied behind these calls, so the answers line up with the sky the camera sees.
 *
 * Radiance carries no exposure. The engine applies exposure to what Shade returns, so a surface
 * never sees it and two surfaces never disagree about it.
 */
public interface ISurfaceLight
{
    /// The direction the sunlight travels, normalized; the vector toward the sun is its negation.
    float3 SunDirection();

    /// The sun's radiance, in the same convention Irradiance shares.
    float3 SunRadiance();

    /// The environment's diffuse irradiance about `dir`. Black when the scene set no environment
    /// lighting, so a surface carries no absent case.
    float3 Irradiance(float3 dir);

    /// The environment's radiance along `dir`, blurred as `roughness` rises from a sharp 0 toward
    /// a fully diffused 1. Black when the scene set no environment lighting.
    float3 Reflection(float3 dir, float roughness);
};
