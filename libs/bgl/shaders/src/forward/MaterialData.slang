import types.EntryBuffer;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;
import idl.ChannelSource;
import idl.IPbrMaterial;
import idl.TextureHandle;
import idl.SamplerHandle;
import forward.common;
import forward.PbrShading;

struct MaterialData
{
    float3 cameraPos;
    EntryBuffer<PbrMaterial> pbrMaterials;
    EntryBuffer<LoosePbrMaterial> looseMaterials;

    TextureHandle irradianceMap;
    TextureHandle prefilterMap;
    TextureHandle brdfLUT;

    SamplerHandle anisioLinearWrapSampler;
    SamplerHandle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData;

extension PbrMaterial : IPbrMaterial
{
    static PbrMaterial Load(uint index)
    {
        return materialData.pbrMaterials[index];
    }

    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
        return baseColorTexture.Sample(s, uv) * baseColorFactor;
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
        return ormTexture.Sample(s, uv).rgb * float3(1.0, roughnessFactor, metallicFactor);
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
        return normalTexture.Sample(s, uv).xy;
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
    return SelectComponent(cs.texture.Sample(s, uv), cs.channel);
}

extension LoosePbrMaterial : IPbrMaterial
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
        return float2(
            SampleLooseChannel(sources[7], s, uv), // X
            SampleLooseChannel(sources[8], s, uv)
        ); // Y
    }
}

public float4 Shade<M : IPbrMaterial>(ForwardVSOut input)
{
    let wrapSampler = materialData.anisioLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    return ShadeSurface(
        input,
        material.GetBaseColor(wrapSampler, input.uv),
        material.GetORM(wrapSampler, input.uv),
        material.GetNormalXY(wrapSampler, input.uv),
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler
    );
}
