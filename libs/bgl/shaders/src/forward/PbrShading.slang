import forward.common;
import idl.TextureHandle;
import idl.SamplerHandle;
import util.Tonemap;

// Fresnel Schlick approximation adjusted for IBL Roughness
public float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

public float3 CalculateNormal(float3 worldNormal, float4 worldTangent, float2 sampledNormalXY)
{
    float3 N = normalize(worldNormal);

    // Normal maps are BC5_UNORM
    float2 xy = sampledNormalXY * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    float3 tangentNormal = float3(xy, z);

    // fall back to the geometric normal when the tangent is degenerate
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

public float4 ShadeSurface(
    ForwardVSOut input,
    float4 baseColor,
    float3 orm,
    float2 normalXY,
    float3 cameraPos,
    TextureCubeHandle irradianceMap,
    TextureCubeHandle prefilterMap,
    TextureHandle brdfLUT,
    SamplerHandle clampSampler,
    float exposure
)
{
    float3 albedo = baseColor.rgb;
    float ao = orm.r;
    float roughness = orm.g;
    float metallic = orm.b;

    // Setup Geometry Vectors
    float3 N = CalculateNormal(input.normal, input.tangent, normalXY);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 R = reflect(-V, N);

    float cosTheta = max(dot(N, V), 0.0);

    // Calculate Surface Reflectivity Base (F0)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 F = FresnelSchlickRoughness(cosTheta, F0, roughness);
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    let irradiance = irradianceMap.SampleCube(clampSampler, N).rgb;
    let diffuse = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 6.0f; // Highest mip index of the 7-level prefilter cube
    let prefilteredColor = prefilterMap.SampleCubeLevel(clampSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 envBRDF = brdfLUT.Sample(clampSampler, float2(cosTheta, roughness)).rg;

    float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    float3 ambient = (kD * diffuse + specular) * ao;

    float3 color = AgX(ambient * exposure);

    return float4(color, baseColor.a);
}
