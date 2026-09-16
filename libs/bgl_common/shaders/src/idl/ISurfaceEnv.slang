// What a surface is lit by, as the shading math reads it: three lookups and one resolution query.
// The math is generic over this so it names no texture; the one implementation today samples
// bindless cubes, and a renderer without them supplies its own.
public interface ISurfaceEnv
{
    float3 Irradiance(float3 dir);
    float3 Prefiltered(float3 dir, float lod);
    uint PrefilterFaceTexels();
    float2 BrdfLut(float cosTheta, float roughness);
};

// The plain half of the same environment, which needs no lookup and so no interface. The sun is
// here rather than behind ISurfaceEnv for exactly that reason: it is data, not a texture, so a
// renderer with no bindless cubes still supplies it the same way.
public struct SurfaceParams
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
