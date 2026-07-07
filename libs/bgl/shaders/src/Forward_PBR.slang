import forward.ForwardData;
import types.EntryBuffer;
import idl.PbrMaterial;
import idl.SamplerHandle;
import forward.common;
import idl.TextureHandle;
import util.Tonemap;

struct MaterialData
{
    float3 cameraPos;
    EntryBuffer<PbrMaterial> pbrMaterials;

    TextureHandle irradianceMap;
    TextureHandle prefilterMap;
    TextureHandle brdfLUT;

    SamplerHandle anisioLinearWrapSampler;
    SamplerHandle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData : register(b0, space1);

// Fresnel Schlick approximation adjusted for IBL Roughness
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Simple normal mapping helper function
float3 CalculateNormal(float3 worldNormal, float4 worldTangent, float3 sampledNormal)
{
    float3 N = normalize(worldNormal);

    // Normal maps are BC5_UNORM
    float2 xy = sampledNormal.xy * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    float3 tangentNormal = float3(xy, z);

    // Gram-Schmidt the tangent against N. A mesh imported without tangents has a zero tangent, which
    // normalize() would turn into NaN and poison every lit pixel, so fall back to the geometric normal
    // when the tangent is degenerate (normal mapping is simply skipped for that surface).
    float3 tangentDir = worldTangent.xyz - dot(worldTangent.xyz, N) * N;
    if (dot(tangentDir, tangentDir) < 1e-8)
    {
        return N;
    }

    float3 T = normalize(tangentDir);

    // Use tangent.w to determine the correct handedness/direction of the bitangent
    float3 B = cross(N, T) * worldTangent.w;

    float3x3 TBN = float3x3(T, B, N);

    // Multiply by TBN to bring the normal into World Space
    return normalize(mul(tangentNormal, TBN));
}

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    // 1. Fetch Material Data from your Megabuffer
    let material = materialData.pbrMaterials[input.materialIndex];

    // Get your samplers mapped from handles
    let wrapSampler = materialData.anisioLinearWrapSampler;
    let clampSampler = materialData.linearClampSampler;

    // 2. Sample Material Textures Bindlessly
    // Note: Adjust GetTexture() to match your engine's bindless fetching syntax
    float4 albedoTex = material.baseColorTexture.Sample(wrapSampler, input.uv);
    float3 normalTex = material.normalTexture.Sample(wrapSampler, input.uv).rgb;
    float3 ormTex = material.ormTexture.Sample(wrapSampler, input.uv).rgb;

    float3 albedo = albedoTex.rgb * material.baseColorFactor.rgb;
    float ao = ormTex.r;
    float roughness = ormTex.g * material.roughnessFactor;
    float metallic = ormTex.b * material.metallicFactor;

    // 3. Setup Geometry Vectors
    float3 N = CalculateNormal(input.normal, input.tangent, normalTex);
    float3 V = normalize(materialData.cameraPos - input.worldPos);
    float3 R = reflect(-V, N);

    float cosTheta = max(dot(N, V), 0.0);

    // 4. Calculate Surface Reflectivity Base (F0)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    // Fresnel
    float3 F = FresnelSchlickRoughness(cosTheta, F0, roughness);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // 5. Ambient Diffuse (Irradiance)
    let irradiance = materialData.irradianceMap.SampleCube(clampSampler, N).rgb;
    let diffuse = irradiance * albedo;

    // 6. Ambient Specular (Prefiltered Environment Map)
    const float MAX_REFLECTION_LOD = 5.0f; // Total mip levels minus one
    let prefilteredColor = materialData.prefilterMap.SampleCubeLevel(clampSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 envBRDF = materialData.brdfLUT.Sample(clampSampler, float2(cosTheta, roughness)).rg;

    float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    // 7. Combine everything with Ambient Occlusion
    float3 ambient = (kD * diffuse + specular) * ao;

    // 8. Exposure + AgX tone map (matches Blender's default view transform; preserves saturation
    //    where ACES skews bright colors toward white). Exposure is a linear pre-scale before the
    //    curve. Output stays LINEAR: the sRGB render target encodes linear→sRGB on write.
    const float exposure = 1.0f;
    float3 color = AgX(ambient * exposure);

    return float4(color, albedoTex.a * material.baseColorFactor.a);
}
