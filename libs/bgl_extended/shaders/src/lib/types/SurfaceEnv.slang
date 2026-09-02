import idl.ISurfaceEnv;
import lib.types.Texture;

// Assembled at the call site rather than nested inside the constant buffer it comes from: a struct
// mixing plain data with resource handles cannot be placed below a constant buffer's top level, and
// the reflection asserts (docs/slang_shaders.md).
public struct SurfaceEnv : ISurfaceEnv
{
    public TextureCube.Handle irradianceMap;
    public TextureCube.Handle prefilterMap;
    public Texture2D.Handle brdfLUT;
    public SamplerState.Handle clampSampler;

    float3 Irradiance(float3 dir) { return irradianceMap.Sample(clampSampler, dir).rgb; }

    float3 Prefiltered(float3 dir, float lod) { return prefilterMap.SampleLevel(clampSampler, dir, lod).rgb; }

    uint PrefilterFaceTexels() { return prefilterMap.CubeFaceTexels(); }

    float2 BrdfLut(float cosTheta, float roughness) { return brdfLUT.Sample(clampSampler, float2(cosTheta, roughness)).rg; }
};
