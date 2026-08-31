import idl.Constants;
import idl.IAlphaTestMaterial;
import idl.IMaterial;
import idl.IPbrMaterial;
import idl.LoosePbrMaterial;
import idl.MaterialType;
import idl.PbrMaterial;
import idl.RawEntry;
import lib.forward.common;
import lib.forward.PbrShading;
import lib.types.RawHandleArena;
import lib.types.Texture;
import lib.util.HashedAlpha;

struct MaterialData
{
    float3 cameraPos;
    float exposure;

    // Advances once per frame, and decorrelates the hashed-alpha pattern between frames. Zero on a
    // target without temporal AA, where a changing pattern would be flicker rather than coverage.
    float alphaHashSeed;

    // The arena and the typed view of the same allocation, as one member: a payload keeps a
    // handle's bytes inline, and the view is the only thing that can make a texture of them.
    // See docs/slang_shaders.md.
    RawHandleArena<Texture2D.Handle> materials;

    TextureCube.Handle irradianceMap;
    TextureCube.Handle prefilterMap;
    Texture2D.Handle brdfLUT;

    // (sin, cos) of the environment's rotation about the up axis -- the same spin the skybox pass
    // puts on its ray. See PbrShading::ToEnvSpace.
    float2 envRotation;

    SamplerState.Handle anisoLinearWrapSampler;
    SamplerState.Handle linearClampSampler;

    // Handle `index` of the record at `byteOffset`, found by position: a payload's handle fields
    // lead it and are contiguous. PbrMaterial.slang and LoosePbrMaterial.slang fix that order.
    Texture2D.Handle GetTextureAt(uint byteOffset, uint index) { return materials.GetHandleAt(byteOffset + cRawPayloadOffset, index); }
};

ConstantBuffer<MaterialData> materialData;

// Which kind of material a reference names, read from the record's own header. For the one pass
// that draws more than one kind; every other is compiled for a single kind and never asks.
public MaterialType LoadMaterialKind(uint byteOffset)
{
    return MaterialType(materialData.materials.LoadTag(RawEntry<IMaterial>.At(byteOffset)));
}

// A payload and where it lives. The interfaces hang off this rather than off the payload, because a
// payload read out of the arena has forgotten its own offset and the handles are found by it.
public struct PbrMaterialRecord : IPbrMaterial, IAlphaTestMaterial
{
    PbrMaterial payload;
    uint byteOffset;

    static PbrMaterialRecord Load(uint byteOffset)
    {
        PbrMaterialRecord record;
        record.payload = materialData.materials.LoadRecordAs<PbrMaterial>(RawEntry<IMaterial>.At(byteOffset));
        record.byteOffset = byteOffset;
        return record;
    }

    float4 GetBaseColor(SamplerState.Handle s, float2 uv)
    {
        return materialData.GetTextureAt(byteOffset, 0).Sample(s, uv) * payload.baseColorFactor;
    }

    float3 GetORM(SamplerState.Handle s, float2 uv)
    {
        return materialData.GetTextureAt(byteOffset, 2).Sample(s, uv).rgb * float3(1.0, payload.roughnessFactor, payload.metallicFactor);
    }

    float2 GetNormalXY(SamplerState.Handle s, float2 uv) { return materialData.GetTextureAt(byteOffset, 1).Sample(s, uv).xy; }

    float GetAlphaCutoff() { return payload.alphaCutoff; }

    float GetTransmission() { return payload.transmissionFactor; }

    float4 GetSpecular() { return payload.specular; }

    float2 GetBaseColorDimensions() { return materialData.GetTextureAt(byteOffset, 0).GetDimensions(); }

    float GetBaseColorAlphaBiased(SamplerState.Handle s, float2 uv, float bias)
    {
        return materialData.GetTextureAt(byteOffset, 0).SampleBias(s, uv, bias).a * payload.baseColorFactor.a;
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

// Channel `i` of a loose record: texture i sampled, then the component its `channels[i]` names.
float SampleLooseChannel(LoosePbrMaterialRecord record, uint i, SamplerState.Handle s, float2 uv)
{
    return SelectComponent(materialData.GetTextureAt(record.byteOffset, i).Sample(s, uv), record.payload.channels[i]);
}

public struct LoosePbrMaterialRecord : IPbrMaterial, IAlphaTestMaterial
{
    LoosePbrMaterial payload;
    uint byteOffset;

    static LoosePbrMaterialRecord Load(uint byteOffset)
    {
        LoosePbrMaterialRecord record;
        record.payload = materialData.materials.LoadRecordAs<LoosePbrMaterial>(RawEntry<IMaterial>.At(byteOffset));
        record.byteOffset = byteOffset;
        return record;
    }

    float4 GetBaseColor(SamplerState.Handle s, float2 uv)
    {
        float4 c = float4(
            SampleLooseChannel(this, 0, s, uv), // R
            SampleLooseChannel(this, 1, s, uv), // G
            SampleLooseChannel(this, 2, s, uv), // B
            SampleLooseChannel(this, 3, s, uv)
        ); // A
        return c * payload.baseColorFactor;
    }

    float3 GetORM(SamplerState.Handle s, float2 uv)
    {
        float ao = SampleLooseChannel(this, 4, s, uv);
        float roughness = SampleLooseChannel(this, 5, s, uv) * payload.roughnessFactor;
        float metallic = SampleLooseChannel(this, 6, s, uv) * payload.metallicFactor;
        return float3(ao, roughness, metallic);
    }

    float2 GetNormalXY(SamplerState.Handle s, float2 uv)
    {
        return float2(
            SampleLooseChannel(this, 7, s, uv), // X
            SampleLooseChannel(this, 8, s, uv)
        ); // Y
    }

    float GetAlphaCutoff() { return payload.alphaCutoff; }

    float GetTransmission() { return payload.transmissionFactor; }

    float4 GetSpecular() { return payload.specular; }

    // The alpha carrier's resolution: coverage machinery cares about the channel being tested,
    // and the loose colour channels may come from other maps at other sizes.
    float2 GetBaseColorDimensions() { return materialData.GetTextureAt(byteOffset, 3).GetDimensions(); }

    float GetBaseColorAlphaBiased(SamplerState.Handle s, float2 uv, float bias)
    {
        return SelectComponent(materialData.GetTextureAt(byteOffset, 3).SampleBias(s, uv, bias), payload.channels[3]) *
               payload.baseColorFactor.a;
    }
}

// The shared tail of both shading paths. Base color is passed in rather than sampled here, so the
// alpha-tested path can test it without sampling the texture a second time.
SurfaceLobes EvaluateWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    return EvaluateSurface(
        input,
        baseColor,
        material.GetORM(wrapSampler, input.uv),
        material.GetNormalXY(wrapSampler, input.uv),
        material.GetSpecular(),
        isFrontFace,
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler,
        materialData.exposure,
        materialData.envRotation
    );
}

float4 ShadeWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor, bool isFrontFace)
{
    let lobes = EvaluateWithBaseColor(input, material, baseColor, isFrontFace);

    return float4(lobes.diffuse + lobes.specular, baseColor.a);
}

public float4 Shade<M : IPbrMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialOffset);

    return ShadeWithBaseColor(input, material, material.GetBaseColor(wrapSampler, input.uv), isFrontFace);
}

/**
 * A blended surface, as premultiplied colour and the coverage that goes with it.
 *
 * The returned colour is already weighted by its own coverage, which is correct only against the
 * transparent PSOs' SrcBlend = One (ForwardPass's c_Psos): either side changed alone is silently
 * wrong rather than a build error. What the transmission factor selects between, and why coverage
 * rises with the reflectance, is in docs/passes.md#blended-surfaces.
 */
public float4 ShadeBlended<M : IPbrMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialOffset);

    let baseColor = material.GetBaseColor(wrapSampler, input.uv);
    let lobes = EvaluateWithBaseColor(input, material, baseColor, isFrontFace);

    let alpha = baseColor.a;
    let transmission = saturate(material.GetTransmission());

    let reflected = max(max(lobes.reflectance.r, lobes.reflectance.g), lobes.reflectance.b);

    let specularWeight = lerp(alpha, 1.0, transmission);
    let coverage = lerp(alpha, alpha + (1.0 - alpha) * reflected, transmission);

    return float4(lobes.diffuse * alpha + lobes.specular * specularWeight, saturate(coverage));
}

public float4 ShadeAlphaTested<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input, bool isFrontFace)
{
    let wrapSampler = materialData.anisoLinearWrapSampler;

    M material = M.Load(input.materialOffset);

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

    M material = M.Load(input.materialOffset);

    let baseColor = material.GetBaseColor(wrapSampler, input.uv);

    // At minification the sampled alpha is many strands averaged, and coverage that honestly
    // matches it is a haze the backdrop swallows. The alpha driving the hash is read one level
    // finer than the footprint, where a sub-texel strand still has shape for the steepening to
    // work against; the mean it steepens about stays one octave coarser than that read, and the
    // sharpening measures its minification in the sampled level's own texels -- the footprint's
    // would be an octave too steep and saturate diluted strands past their area.
    let texDim = material.GetBaseColorDimensions() * exp2(c_HashedAlphaLodBias);
    let hashAlpha = material.GetBaseColorAlphaBiased(wrapSampler, input.uv, c_HashedAlphaLodBias);
    let meanAlpha = material.GetBaseColorAlphaBiased(wrapSampler, input.uv, c_HashedAlphaLodBias + c_MinifiedAlphaMeanBias);

    float alpha = SharpenMinifiedAlpha(hashAlpha, meanAlpha, input.uv, texDim);

    if (alpha < HashedAlphaThreshold(input.worldPos, materialData.alphaHashSeed))
    {
        discard;
    }

    return ShadeWithBaseColor(input, material, baseColor, isFrontFace);
}
