import types.EntryBuffer;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;
import idl.ChannelSource;
import idl.IPbrMaterial;
import idl.IAlphaTestMaterial;
import idl.SamplerHandle;
import idl.TextureHandle;
import forward.common;
import forward.PbrShading;

struct MaterialData
{
    float3 cameraPos;
    float exposure;

    EntryBuffer<PbrMaterial> pbrMaterials;
    EntryBuffer<LoosePbrMaterial> looseMaterials;

    TextureCubeHandle irradianceMap;
    TextureCubeHandle prefilterMap;
    Texture2DHandle brdfLUT;

    SamplerHandle anisoLinearWrapSampler;
    SamplerHandle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData;

extension PbrMaterial : IPbrMaterial, IAlphaTestMaterial
{
    static PbrMaterial Load(uint index)
    {
        return materialData.pbrMaterials[index];
    }

    // The BGL_WGSL arms shade from factors alone: a material-table texture is a uint2 there, with
    // nothing to dereference it against until the W4 atlas. (0.5, 0.5) decodes to the flat normal.
    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
#if defined(BGL_WGSL)
        return baseColorFactor;
#else
        return baseColorTexture.Sample(s, uv) * baseColorFactor;
#endif
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
#if defined(BGL_WGSL)
        return float3(1.0, roughnessFactor, metallicFactor);
#else
        return ormTexture.Sample(s, uv).rgb * float3(1.0, roughnessFactor, metallicFactor);
#endif
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
#if defined(BGL_WGSL)
        return float2(0.5, 0.5);
#else
        return normalTexture.Sample(s, uv).xy;
#endif
    }

    float GetAlphaCutoff()
    {
        return alphaCutoff;
    }
}

// Selects one component of `v`. Indexing a vector with a runtime value (`v[c]`) makes DXC spill it
// to an indexable temp; an explicit switch lowers to a select chain instead, with no scratch memory.
float SelectComponent(float4 v, uint c)
{
    switch (c)
    {
    case 0:
        return v.x;
    case 1:
        return v.y;
    case 2:
        return v.z;
    default:
        return v.w;
    }
}

float SampleLooseChannel(ChannelSource cs, SamplerHandle s, float2 uv)
{
#if defined(BGL_WGSL)
    // No texture to sample (see the PbrMaterial arms); 1.0 leaves the caller's factor as the value.
    return 1.0;
#else
    return SelectComponent(cs.texture.Sample(s, uv), cs.channel);
#endif
}

extension LoosePbrMaterial : IPbrMaterial, IAlphaTestMaterial
{
    static LoosePbrMaterial Load(uint index)
    {
        return materialData.looseMaterials[index];
    }

    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
        float4 c = float4(
            SampleLooseChannel(sources[0], s, uv), // R
            SampleLooseChannel(sources[1], s, uv), // G
            SampleLooseChannel(sources[2], s, uv), // B
            SampleLooseChannel(sources[3], s, uv)
        ); // A
        return c * baseColorFactor;
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
        float ao = SampleLooseChannel(sources[4], s, uv);
        float roughness = SampleLooseChannel(sources[5], s, uv) * roughnessFactor;
        float metallic = SampleLooseChannel(sources[6], s, uv) * metallicFactor;
        return float3(ao, roughness, metallic);
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
#if defined(BGL_WGSL)
        // Not through SampleLooseChannel: its 1.0 decodes to a degenerate tangent normal, not flat.
        return float2(0.5, 0.5);
#else
        return float2(
            SampleLooseChannel(sources[7], s, uv), // X
            SampleLooseChannel(sources[8], s, uv)
        ); // Y
#endif
    }

    float GetAlphaCutoff()
    {
        return alphaCutoff;
    }
}

// The shared tail of both shading paths. Base color is passed in rather than sampled here, so the
// alpha-tested path can test it without sampling the texture a second time.
float4 ShadeWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    return ShadeSurface(
        input,
        baseColor,
        material.GetORM(wrapSampler, input.uv),
        material.GetNormalXY(wrapSampler, input.uv),
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler,
        materialData.exposure
    );
}

public float4 Shade<M : IPbrMaterial>(ForwardVSOut input)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    return ShadeWithBaseColor(input, material, material.GetBaseColor(wrapSampler, input.uv));
}

public float4 ShadeAlphaTested<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    let baseColor = material.GetBaseColor(wrapSampler, input.uv);

#if defined(BGL_WGSL)
    // Shade first, discard after: Tint requires textureSample in uniform control flow and treats
    // everything after a discard as non-uniform, so the IBL samples may not follow it.
    let shaded = ShadeWithBaseColor(input, material, baseColor);

    if (baseColor.a < material.GetAlphaCutoff())
    {
        discard;
    }

    return shaded;
#else
    if (baseColor.a < material.GetAlphaCutoff())
    {
        discard;
    }

    return ShadeWithBaseColor(input, material, baseColor);
#endif
}

#if defined(BGL_WGSL)
// The transparent pass branches on materialIsLoose, which varies per fragment -- and Tint requires
// textureSample in uniform control flow. On this target the material reads are sample-free
// (factors only), so they gather under the branch and the IBL samples run once, outside it.
public float4 ShadeEitherMaterial(ForwardVSOut input)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    float4 baseColor;
    float3 orm;
    float2 normalXY;

    if (input.materialIsLoose != 0u)
    {
        let material = LoosePbrMaterial.Load(input.materialIndex);
        baseColor = material.GetBaseColor(wrapSampler, input.uv);
        orm = material.GetORM(wrapSampler, input.uv);
        normalXY = material.GetNormalXY(wrapSampler, input.uv);
    }
    else
    {
        let material = PbrMaterial.Load(input.materialIndex);
        baseColor = material.GetBaseColor(wrapSampler, input.uv);
        orm = material.GetORM(wrapSampler, input.uv);
        normalXY = material.GetNormalXY(wrapSampler, input.uv);
    }

    return ShadeSurface(
        input,
        baseColor,
        orm,
        normalXY,
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler,
        materialData.exposure
    );
}
#endif

// Depth-only pre-pass for a self-occluding blend material: keep only the fragments whose base-color
// alpha clears the cutoff, so the front layer's depth is what the Equal-tested colour pass matches.
// Writes no colour; the pipeline binds no render target.
public void PrepassDiscard<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    if (material.GetBaseColor(wrapSampler, input.uv).a < material.GetAlphaCutoff())
    {
        discard;
    }
}
