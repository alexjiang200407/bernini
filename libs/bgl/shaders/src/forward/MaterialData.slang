import types.EntryBuffer;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;
import idl.ChannelSource;
import idl.IPbrMaterial;
import idl.IAlphaTestMaterial;
import idl.TextureHandle;
import forward.common;
import forward.PbrShading;
import util.HashedAlpha;

struct MaterialData
{
    float3 cameraPos;
    float exposure;

    // Advances once per frame, and decorrelates the hashed-alpha pattern between frames. Zero on a
    // target without temporal AA, where a changing pattern would be flicker rather than coverage.
    float alphaHashSeed;

    EntryBuffer<PbrMaterial> pbrMaterials;
    EntryBuffer<LoosePbrMaterial> looseMaterials;

    TextureCubeHandle irradianceMap;
    TextureCubeHandle prefilterMap;
    TextureHandle brdfLUT;

    SamplerState.Handle anisoLinearWrapSampler;
    SamplerState.Handle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData;

extension PbrMaterial : IPbrMaterial, IAlphaTestMaterial
{
    static PbrMaterial Load(uint index)
    {
        return materialData.pbrMaterials[index];
    }

    float4 GetBaseColor(SamplerState.Handle s, float2 uv)
    {
        return baseColorTexture.Sample(s, uv) * baseColorFactor;
    }

    float3 GetORM(SamplerState.Handle s, float2 uv)
    {
        return ormTexture.Sample(s, uv).rgb * float3(1.0, roughnessFactor, metallicFactor);
    }

    float2 GetNormalXY(SamplerState.Handle s, float2 uv)
    {
        return normalTexture.Sample(s, uv).xy;
    }

    float GetAlphaCutoff()
    {
        return alphaCutoff;
    }

    float2 GetBaseColorDimensions()
    {
        return baseColorTexture.Dimensions();
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

float SampleLooseChannel(ChannelSource cs, SamplerState.Handle s, float2 uv)
{
    return SelectComponent(cs.texture.Sample(s, uv), cs.channel);
}

extension LoosePbrMaterial : IPbrMaterial, IAlphaTestMaterial
{
    static LoosePbrMaterial Load(uint index)
    {
        return materialData.looseMaterials[index];
    }

    float4 GetBaseColor(SamplerState.Handle s, float2 uv)
    {
        float4 c = float4(
            SampleLooseChannel(sources[0], s, uv), // R
            SampleLooseChannel(sources[1], s, uv), // G
            SampleLooseChannel(sources[2], s, uv), // B
            SampleLooseChannel(sources[3], s, uv)
        ); // A
        return c * baseColorFactor;
    }

    float3 GetORM(SamplerState.Handle s, float2 uv)
    {
        float ao = SampleLooseChannel(sources[4], s, uv);
        float roughness = SampleLooseChannel(sources[5], s, uv) * roughnessFactor;
        float metallic = SampleLooseChannel(sources[6], s, uv) * metallicFactor;
        return float3(ao, roughness, metallic);
    }

    float2 GetNormalXY(SamplerState.Handle s, float2 uv)
    {
        return float2(
            SampleLooseChannel(sources[7], s, uv), // X
            SampleLooseChannel(sources[8], s, uv)
        ); // Y
    }

    float GetAlphaCutoff()
    {
        return alphaCutoff;
    }

    // The alpha carrier's resolution: coverage machinery cares about the channel being tested,
    // and the loose colour channels may come from other maps at other sizes.
    float2 GetBaseColorDimensions()
    {
        return sources[3].texture.Dimensions();
    }
}

// The shared tail of both shading paths. Base color is passed in rather than sampled here, so the
// alpha-tested path can test it without sampling the texture a second time.
float4 ShadeWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    return ShadeSurface(
        input,
        baseColor,
        material.GetORM(wrapSampler, input.uv),
        material.GetNormalXY(wrapSampler, input.uv),
        isFrontFace,
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler,
        materialData.exposure
    );
}

public float4 Shade<M : IPbrMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    return ShadeWithBaseColor(input, material, material.GetBaseColor(wrapSampler, input.uv), isFrontFace);
}

public float4 ShadeAlphaTested<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    let baseColor = material.GetBaseColor(wrapSampler, input.uv);

    if (baseColor.a < material.GetAlphaCutoff())
    {
        discard;
    }

    return ShadeWithBaseColor(input, material, baseColor, isFrontFace);
}

// Stochastic coverage in place of a cutoff: the fragment survives with probability equal to its
// alpha, and writes real depth like any opaque surface. One frame of it is noise; the ensemble over
// pixels and frames is the blend, which is why this pairs with temporal AA and not without it.
//
// The material's alphaCutoff is deliberately unused. A cutoff is the thing being replaced, and
// applying both would put back the binary silhouette that costs the surface its layers.
public float4 ShadeHashedAlpha<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    let baseColor = material.GetBaseColor(wrapSampler, input.uv);

    // At minification the sampled alpha is many strands averaged, and coverage that honestly
    // matches it is a haze the backdrop swallows; sharpening it back toward the cutoff's
    // isocontour keeps distant strands the crisp features the alpha test would draw, and leaves
    // magnified alpha -- where hashed earns its keep -- untouched.
    float alpha = SharpenMinifiedAlpha(baseColor.a, material.GetAlphaCutoff(), input.uv, material.GetBaseColorDimensions());

    if (alpha < HashedAlphaThreshold(input.worldPos, materialData.alphaHashSeed))
    {
        discard;
    }

    return ShadeWithBaseColor(input, material, baseColor, isFrontFace);
}
