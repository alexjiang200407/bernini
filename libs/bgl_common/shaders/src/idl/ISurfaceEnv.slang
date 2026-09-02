// THIS IS A FILE GENERATED FROM ISurfaceEnv.slang. DO NOT EDIT MANUALLY

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

// The plain half of the same environment, which needs no lookup and so no interface.
public struct SurfaceParams
{
    public float3 cameraPos;
    public float exposure;
    /// (sin, cos) of the environment's rotation about the up axis.
    public float2 envRotation;
};
