import idl.Constants;
import idl.IAlphaTestMaterial;
import idl.IMaterial;
import idl.IPbrMaterial;
import idl.LoosePbrMaterial;
import idl.MaterialType;
import idl.PbrMaterial;
import idl.RawEntry;
import lib.forward.PbrShading;
import lib.types.RawHandleArena;
import lib.types.Texture;

public struct MaterialData
{
    public float3 cameraPos;
    public float exposure;

    // Advances once per frame, and decorrelates the hashed-alpha pattern between frames. Zero on a
    // target without temporal AA, where a changing pattern would be flicker rather than coverage.
    public float alphaHashSeed;

    // The arena and the typed view of the same allocation, as one member: a payload keeps a
    // handle's bytes inline, and the view is the only thing that can make a texture of them.
    // See docs/slang_shaders.md.
    public RawHandleArena<Texture2D.Handle> materials;

    public TextureCube.Handle irradianceMap;
    public TextureCube.Handle prefilterMap;
    public Texture2D.Handle brdfLUT;

    // (sin, cos) of the environment's rotation about the up axis -- the same spin the skybox pass
    // puts on its ray. See PbrShading::ToEnvSpace.
    public float2 envRotation;

    public SamplerState.Handle anisoLinearWrapSampler;
    public SamplerState.Handle linearClampSampler;

    // Handle `index` of the record at `byteOffset`, found by position: a payload's handle fields
    // lead it and are contiguous. PbrMaterial.slang and LoosePbrMaterial.slang fix that order.
    public Texture2D.Handle GetTextureAt(uint byteOffset, uint index)
    {
        return materials.GetHandleAt(byteOffset + cRawPayloadOffset, index);
    }

    public SurfaceEnv GetSurfaceEnv()
    {
        SurfaceEnv env;
        env.cameraPos = cameraPos;
        env.exposure = exposure;
        env.irradianceMap = irradianceMap;
        env.prefilterMap = prefilterMap;
        env.brdfLUT = brdfLUT;
        env.clampSampler = linearClampSampler;
        env.envRotation = envRotation;
        return env;
    }
};

public ConstantBuffer<MaterialData> materialData;

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
