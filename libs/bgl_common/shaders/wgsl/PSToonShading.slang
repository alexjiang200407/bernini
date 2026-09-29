// WGSL-lowering driver for lib.math.ToonShading. Fragment stage, where a surface is shaded; a
// constant reader and light stand in for the renderer's, and each model's adapter is driven
// through the lit contract a registered surface is drawn by.
import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.ToonCharacterSurface;
import bgl.ToonEnvironmentSurface;
import lib.math.ToonShading;

struct ConstantReader : IMaterialReader
{
    float3 normal;

    float4 Sample<S : ISlot>(S slot, float2 uv) { return float4(uv, float(slot.Index()), 1.0); }
    float2 Uv() { return float2(0.25, 0.75); }
    float2 Uv1() { return float2(-1.0, -1.0); }
    bool HasUv1() { return false; }
    float3 WorldPos() { return float3(0.0, 0.0, 0.0); }
    float3 WorldNormal() { return normal; }
    float4 WorldTangent() { return float4(1.0, 0.0, 0.0, 1.0); }
    float3 CameraPos() { return float3(0.0, 0.0, 5.0); }
};

struct ConstantLight : ISurfaceLight
{
    float3 SunDirection() { return float3(0.0, -1.0, 0.0); }
    float3 SunRadiance() { return float3(1.0, 1.0, 1.0); }
    float3 Irradiance(float3 dir) { return float3(0.1, 0.1, 0.1); }
    float3 Reflection(float3 dir, float roughness) { return float3(0.1, 0.1, 0.1); }
};

struct FlatParams
{
    float4 baseColorFactor;
    ColorSlot baseColor;
};

struct FlatCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params) { return params.baseColorFactor.a; }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};

struct FlatEnvironment : IToonEnvironmentSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params) { return params.baseColorFactor.a; }

    static ToonEnvironmentSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonEnvironmentSurface surface = ToonEnvironmentSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};

[shader("fragment")]
float4 main(float3 normal: NORMAL, float4 factor: COLOR) : SV_Target
{
    ConstantReader reader = { normal };
    ConstantLight light;
    FlatParams params = { factor, { 0u } };

    let character = ToonCharacterLit<FlatCharacter>.Shade(reader, light, params);
    let environment = ToonEnvironmentLit<FlatEnvironment>.Shade(reader, light, params);
    let coverage = ToonCharacterLit<FlatCharacter>.Coverage(reader, params) * ToonEnvironmentLit<FlatEnvironment>.Coverage(reader, params);
    return (character + environment) * coverage;
}
