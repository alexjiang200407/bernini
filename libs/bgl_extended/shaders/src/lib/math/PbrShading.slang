import bgl.PbrSurface;
import idl.ISurfaceEnv;
import idl.ShadingParams;

// Fresnel Schlick approximation adjusted for IBL Roughness
public float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Schlick as written, for a light with a direction: the angle is the view against the half vector,
// where the roughness-adjusted form above answers for a whole environment at the view angle.
public float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/**
 * The GGX/Trowbridge-Reitz normal distribution: what fraction of the surface's microfacets face
 * `H`. `alpha` is the squared perceptual roughness, which is the remapping the rest of this file
 * assumes -- the LOD comment below states it as `a = roughness^2`.
 */
public float DistributionGGX(float NdotH, float alpha)
{
    let a2 = alpha * alpha;
    let d = NdotH * NdotH * (a2 - 1.0) + 1.0;

    return a2 / max(3.14159265 * d * d, 1e-8);
}

/**
 * Smith's height-correlated masking-shadowing, as the *visibility* term -- `G` with the BRDF's
 * `1 / (4 NdotL NdotV)` already folded in.
 *
 * Folded rather than left separate because that denominator and `G` both go to zero at grazing
 * angles, and dividing one by the other there is where a specular lobe produces the white rim
 * nobody can source. Heitz's derivation; the pairing is what Frostbite and Filament ship.
 */
public float VisibilitySmithGGXCorrelated(float NdotV, float NdotL, float alpha)
{
    let a2 = alpha * alpha;
    let v = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    let l = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);

    return 0.5 / max(v + l, 1e-8);
}

/**
 * The shading normal, with the normal map applied in the surface's tangent frame.
 *
 * `isFrontFace` is the rasterizer's own answer, from SV_IsFrontFace. A two-sided pipeline
 * (RasterCullMode::kNone -- every bucket that draws a material) rasterizes the back of a surface
 * with the interpolated normal still pointing away from the camera, which sends the view angle, the
 * irradiance lookup and the reflection vector all to the wrong hemisphere.
 *
 * The flip is applied to the geometric normal *before* the frame is built rather than to the result,
 * so the bitangent flips with it. Negating the shaded normal afterwards leaves a mirrored bitangent
 * and leans the normal map's detail the wrong way on every back face.
 */
public float3 CalculateNormal(float3 worldNormal, float4 worldTangent, float2 sampledNormalXY, bool isFrontFace)
{
    float3 N = normalize(isFrontFace ? worldNormal : -worldNormal);

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

/**
 * Turns a world direction into the direction that names the same light in the environment's cubes.
 *
 * The skybox spins its clip-to-world ray by BEnv::skyRotationY, so the sky a camera sees along `dir`
 * is the texel at the rotated direction. The IBL cubes are the same environment, so a normal has to
 * be carried through the same rotation or the model is lit from where the sky used to be.
 *
 * @param envRotation World to environment space; a pure rotation.
 */
float3 ToEnvSpace(float3 dir, float3x3 envRotation)
{
    return mul(envRotation, dir);
}

/// Highest mip index of the 7-level prefilter chain: mip N is convolved for roughness N/6, so
/// every reader maps roughness onto the chain by this and nothing else (docs/envmaps.md).
public static const float c_PrefilterMaxLod = 6.0;

/**
 * The two ambient lobes of a surface, kept apart because a blended surface weights them differently
 * (docs/passes.md#blended-surfaces). Summed, the pair is the opaque answer.
 */
public struct SurfaceLobes
{
    public float3 diffuse;
    public float3 specular;

    /// The fraction of the environment the specular lobe returns, per channel. Exposure is not in it
    /// -- it is a ratio, not radiance.
    public float3 reflectance;

    /// What the surface emits, exposed like the two lobes. Not a lobe: no light is involved.
    public float3 emissive;
};

/**
 * A blended surface as premultiplied colour and the coverage that goes with it.
 *
 * `transmission` selects what `alpha` means: 0 for coverage, 1 for a surface the light passes
 * through, whose specular and emissive stay whole and whose coverage rises with its reflectance.
 * Why, and what the blend state on the other side has to be, is docs/passes.md#blended-surfaces.
 */
public float4 BlendedSurface(SurfaceLobes lobes, float alpha, float transmission)
{
    let reflected = max(max(lobes.reflectance.r, lobes.reflectance.g), lobes.reflectance.b);

    // Light that leaves the surface itself -- reflected or emitted -- is what transmission exempts
    // from thinning; only the diffuse lobe is the light that came through from behind.
    let surfaceWeight = lerp(alpha, 1.0, transmission);
    let coverage = lerp(alpha, alpha + (1.0 - alpha) * reflected, transmission);

    return float4(lobes.diffuse * alpha + (lobes.specular + lobes.emissive) * surfaceWeight, saturate(coverage));
}

public SurfaceLobes EvaluateSurface<E : ISurfaceEnv>(
    E env,
    ShadingParams params,
    float3 worldPos,
    float3 worldNormal,
    float4 worldTangent,
    PbrSurface surface,
    bool isFrontFace
)
{
    float3 albedo = surface.baseColor.rgb;
    float ao = surface.orm.r;
    float roughness = surface.orm.g;
    float metallic = surface.orm.b;
    float4 specularFactors = surface.specularFactors;

    // Setup Geometry Vectors
    float3 N = CalculateNormal(worldNormal, worldTangent, surface.normalXY, isFrontFace);
    float3 V = normalize(params.cameraPos - worldPos);
    float3 R = reflect(-V, N);

    float cosTheta = max(dot(N, V), 0.0);

    // Calculate Surface Reflectivity Base (F0)
    float3 F0 = min(float3(0.04, 0.04, 0.04) * specularFactors.rgb, float3(1.0, 1.0, 1.0));
    F0 = lerp(F0, albedo, metallic);

    // KHR_materials_specular weights the dielectric lobe alone: a metal's reflection is its base
    // colour, and the extension leaves it be.
    float specularWeight = lerp(saturate(specularFactors.a), 1.0, metallic);

    float3 F = FresnelSchlickRoughness(cosTheta, F0, roughness) * specularWeight;
    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    let irradiance = env.Irradiance(ToEnvSpace(N, params.envRotation));
    let diffuse = irradiance * albedo;

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
    const float texel0 = 1.5707963f / float(env.PrefilterFaceTexels());
    const float excess = max(footprint - texel0 * 3.0f, 0.0f);
    const float areaLod = min(sqrt(excess) * c_PrefilterMaxLod, c_AreaLodCap);

    const float lod = min(max(roughness * c_PrefilterMaxLod, areaLod), c_PrefilterMaxLod);
    let prefilteredColor = env.Prefiltered(ToEnvSpace(R, params.envRotation), lod);
    float2 envBRDF = env.BrdfLut(cosTheta, roughness);

    // The sun. Its radiance is in the irradiance map's 1/pi convention, so it lands in the diffuse
    // lobe on exactly the terms the map's own value does and the two are directly comparable.
    //
    // Not scaled by `ao`: that is a visibility estimate for light arriving from every direction, and
    // this light arrives from one. What occludes a directional light is a shadow, which nothing here
    // casts -- so a surface facing the sun is lit whatever stands in front of it.
    //
    // The dielectric/metal split is `1 - metallic` rather than the `kD` above, because `kD` comes
    // from FresnelSchlickRoughness at the *view* angle, which is the split-sum's approximation for
    // an environment and not the energy a single direction hands to the diffuse lobe.
    float3 L = -params.sunDirection;
    float sunNdotL = max(dot(N, L), 0.0);
    float3 sunDiffuse = albedo * params.sunRadiance * sunNdotL * (1.0 - metallic);

    // The sun's specular lobe, Cook-Torrance against the same F0 and dielectric weight the
    // environment's half uses -- one statement of what the surface reflects, two lights asking it.
    //
    // The pi is the 1/pi convention arriving from the other side. `sunRadiance` is what the
    // irradiance map would hold, and that map carries E/pi, so the true irradiance this lobe
    // integrates against is pi times it. The diffuse term above never sees the factor because
    // Lambert's own 1/pi cancels it; here there is nothing to cancel against, and dropping it is
    // the easiest mistake in this file to make and the hardest to see.
    float3 H = normalize(V + L);
    float sunNdotV = max(cosTheta, 1e-4);
    float alpha = roughness * roughness;

    float3 sunF = FresnelSchlick(max(dot(V, H), 0.0), F0) * specularWeight;
    float sunD = DistributionGGX(max(dot(N, H), 0.0), alpha);
    float sunVis = VisibilitySmithGGXCorrelated(sunNdotV, sunNdotL, alpha);

    float3 sunSpecular = sunF * (sunD * sunVis * 3.14159265 * sunNdotL) * params.sunRadiance;

    SurfaceLobes lobes;
    // envBRDF.y is the split-sum's F90 term, which the weight scales as much as F0 -- it is already
    // in F. Left unweighted, a surface with the specular switched off keeps a grazing rim of it.
    lobes.reflectance = (F * envBRDF.x + envBRDF.y * specularWeight) * ao;
    lobes.diffuse = (kD * diffuse * ao + sunDiffuse) * params.exposure;
    // `reflectance` stays the environment's own ratio and the sun is added past it: it is what a
    // blended surface raises its coverage by, and a sun's radiance is not a fraction of anything.
    // The highlight still reaches a transmissive surface -- the lobe is summed either way, and the
    // shared blend PSO adds premultiplied colour (docs/passes.md#blended-surfaces).
    lobes.specular = (prefilteredColor * lobes.reflectance + sunSpecular) * params.exposure;
    lobes.emissive = surface.emissive * params.exposure;

    return lobes;
}
