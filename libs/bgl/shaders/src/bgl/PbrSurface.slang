/**
 * What the PBR model reads off a surface: the material's half of shading. The lighting's half is
 * SurfaceLobes in lib.math.PbrShading, which a surface never sees. Another model reads another struct.
 *
 * Each default is what a surface that says nothing about the field means: white, a matte
 * dielectric, a flat normal, glTF's specular, no transmission, no emission.
 */
public struct PbrSurface
{
    public float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);

    /// Occlusion, roughness, metallic.
    public float3 orm = float3(1.0, 1.0, 0.0);

    /// A tangent-space normal's xy in [0, 1], as a BC5 normal map stores it.
    public float2 normalXY = float2(0.5, 0.5);

    /// glTF's KHR_materials_specular: rgb tints a dielectric's F0, a weights the whole lobe.
    public float4 specularFactors = float4(1.0, 1.0, 1.0, 1.0);

    /// glTF's KHR_materials_transmission factor, not a lobe: under blend it selects whether base
    /// alpha means coverage (0) or transmission (1). Read by no other layer.
    public float transmissionFactor = 0.0;

    /// Scene-linear radiance the surface emits, under the same exposure as the lobes.
    public float3 emissive = float3(0.0, 0.0, 0.0);
};
