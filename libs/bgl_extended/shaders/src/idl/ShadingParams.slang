// What is shining on a surface, as plain data: the two lights, and the camera the BRDF measures them
// against.
//
// Nothing here is a property of the surface -- that is bgl.PbrSurface, which the material fills --
// and nothing here needs a lookup, which is what keeps it out of idl.ISurfaceEnv beside it. A
// renderer with no bindless cubes implements that interface differently and supplies this unchanged.
public struct ShadingParams
{
    public float3 cameraPos;
    public float exposure;

    /// World to environment space, the rotation the cubes are looked up through.
    public float3x3 envRotation;

    /// The direction the sun travels, normalized. Negated once to shade; see bgl/DirectionalLightDesc.h.
    public float3 sunDirection;

    /// The sun's colour scaled by its intensity, in the units the irradiance map holds. Zero when
    /// no sun is set, which is the default and costs one multiply.
    public float3 sunRadiance;
};
