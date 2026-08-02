import forward.common;
import idl.TextureHandle;

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
    SamplerState.Handle clampSampler,
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

    // Roughness alone is not enough to pick the mip. A mirror asks for mip 0 at every distance, so a
    // sphere small on screen has each pixel covering a wide cone of the reflection while still
    // fetching full resolution -- which aliases into grain. The chain's mips exist; an explicit
    // SampleLevel just never consults the derivatives the way a plain Sample would.
    //
    // So floor the LOD by the reflection's screen-space footprint. |dR| is roughly the angle in
    // radians that one pixel subtends in the reflection. max(), never a sum: roughness still sets the
    // minimum blur and this only ever coarsens.
    //
    // Match the footprint to the mip's *lobe*, not to its texel size. This chain indexes roughness,
    // so mip N is half-resolution AND convolved for roughness N/6 -- and that convolution is several
    // times wider than the texel it sits on. Choosing the mip whose texels match the footprint
    // over-blurs badly: a 1.8 degree footprint lands on mip 2.35, whose lobe is about 8.6 degrees, and
    // a distant mirror turns into a featureless pearl. A GGX half-angle is roughly a = roughness^2, so
    // roughness = sqrt(footprint) blurs by as much as the pixel covers and no more.
    //
    // And leave a dead zone, because mip 0 already blurs by its own texel. Correcting every footprint
    // past a single texel touches even a mirror filling the screen -- at 450 px a sphere still spans
    // 2.3 texels per pixel -- and trades sharpness for aliasing that nobody minds. A mirror is judged
    // on being sharp, so bias toward that: below a few texels of footprint, do nothing at all.
    // And cap it, because on this chain every mip of coarsening costs a whole roughness step of lobe
    // and not merely half the resolution. Left unbounded the blur keeps compounding as the camera
    // pulls back, which reads as a mirror dissolving rather than as a mirror seen from further away.
    // Capped, the correction takes the worst of the grain off and then stops; the residue at extreme
    // distance is grain on a handful of pixels, which is the cheaper of the two artefacts.
    //
    // Bounding it here rather than sampling correctly is a compromise. The principled fix is a
    // resolution-only mip chain of the sharp environment to sample for the footprint, so that
    // anti-aliasing costs resolution alone -- this chain cannot express that, because its mips are
    // roughness.
    const float c_AreaLodCap = 1.0f;

    const float footprint = max(length(ddx(R)), length(ddy(R)));
    const float texel0 = 1.5707963f / float(prefilterMap.CubeFaceTexels());
    const float excess = max(footprint - texel0 * 3.0f, 0.0f);
    const float areaLod = min(sqrt(excess) * MAX_REFLECTION_LOD, c_AreaLodCap);

    const float lod = min(max(roughness * MAX_REFLECTION_LOD, areaLod), MAX_REFLECTION_LOD);
    let prefilteredColor = prefilterMap.SampleCubeLevel(clampSampler, R, lod).rgb;
    float2 envBRDF = brdfLUT.Sample(clampSampler, float2(cosTheta, roughness)).rg;

    float3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    float3 ambient = (kD * diffuse + specular) * ao;

    float3 color = ambient * exposure;

    return float4(color, baseColor.a);
}
