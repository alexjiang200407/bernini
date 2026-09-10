// WGSL-lowering driver for lib.math.PbrShading. Fragment stage: the footprint LOD takes derivatives
// of the reflection vector. The environment here is plain cube and 2D textures -- the first
// ISurfaceEnv with no bindless handle.
import bgl.PbrSurface;
import idl.ISurfaceEnv;
import lib.math.PbrShading;

TextureCube gIrradiance;
TextureCube gPrefilter;
Texture2D gBrdfLut;
SamplerState gClamp;

struct BoundEnv : ISurfaceEnv
{
    float3 Irradiance(float3 dir) { return gIrradiance.Sample(gClamp, dir).rgb; }
    float3 Prefiltered(float3 dir, float lod) { return gPrefilter.SampleLevel(gClamp, dir, lod).rgb; }
    uint PrefilterFaceTexels()
    {
        uint w;
        uint h;
        gPrefilter.GetDimensions(w, h);
        return w;
    }
    float2 BrdfLut(float cosTheta, float roughness) { return gBrdfLut.Sample(gClamp, float2(cosTheta, roughness)).rg; }
};

[shader("fragment")]
float4 main(float3 worldPos: POSITION, float3 normal: NORMAL, float4 tangent: TANGENT) : SV_Target
{
    BoundEnv env;
    SurfaceParams params = { float3(0.0, 1.0, 0.0), 1.0, float3x3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0) };
    PbrSurface surface;
    surface.baseColor = float4(0.5, 0.5, 0.5, 1.0);
    surface.orm = float3(1.0, 0.5, 0.0);
    surface.normalXY = float2(0.5, 0.5);
    surface.specularFactors = float4(1.0, 1.0, 1.0, 1.0);
    surface.transmissionFactor = 0.5;
    surface.emissive = float3(0.1, 0.0, 0.0);
    let lobes = EvaluateSurface(env, params, worldPos, normal, tangent, surface, true);
    let n = CalculateNormal(normal, tangent, float2(0.5, 0.5), true);
    let f = FresnelSchlickRoughness(0.5, float3(0.04, 0.04, 0.04), 0.5);
    let blended = BlendedSurface(lobes, 0.5, surface.transmissionFactor);
    return float4(lobes.diffuse + lobes.specular * lobes.reflectance + lobes.emissive + n * f.x, 1.0) + blended;
}
