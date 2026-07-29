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

// The material reads fork by target -- sampled on DXIL, factor-only where the material tables'
// textures cannot be dereferenced (see the arms themselves) -- and the two files export identical
// signatures, so everything below is target-blind.
#if defined(BGL_WGSL)
import forward.MaterialFactors;
#else
import forward.MaterialSampled;
#endif

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

    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
        return MaterialBaseColor(this, s, uv);
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
        return MaterialOrm(this, s, uv);
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
        return MaterialNormalXY(this, s, uv);
    }

    float GetAlphaCutoff()
    {
        return alphaCutoff;
    }
}

extension LoosePbrMaterial : IPbrMaterial, IAlphaTestMaterial
{
    static LoosePbrMaterial Load(uint index)
    {
        return materialData.looseMaterials[index];
    }

    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
        return MaterialBaseColor(this, s, uv);
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
        return MaterialOrm(this, s, uv);
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
        return MaterialNormalXY(this, s, uv);
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
