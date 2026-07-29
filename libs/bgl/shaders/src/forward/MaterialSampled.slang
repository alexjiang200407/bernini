import idl.ChannelSource;
import idl.LoosePbrMaterial;
import idl.PbrMaterial;
import idl.SamplerHandle;
import idl.TextureHandle;

// The sampled material reads: the DXIL arm of the pair MaterialData.slang imports one of, chosen
// by target. MaterialFactors.slang is the other; the two export identical signatures.

public float4 MaterialBaseColor(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return m.baseColorTexture.Sample(s, uv) * m.baseColorFactor;
}

public float3 MaterialOrm(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return m.ormTexture.Sample(s, uv).rgb * float3(1.0, m.roughnessFactor, m.metallicFactor);
}

public float2 MaterialNormalXY(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return m.normalTexture.Sample(s, uv).xy;
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

public float4 MaterialBaseColor(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    float4 c = float4(
        SampleLooseChannel(m.sources[0], s, uv), // R
        SampleLooseChannel(m.sources[1], s, uv), // G
        SampleLooseChannel(m.sources[2], s, uv), // B
        SampleLooseChannel(m.sources[3], s, uv)
    ); // A
    return c * m.baseColorFactor;
}

public float3 MaterialOrm(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    float ao = SampleLooseChannel(m.sources[4], s, uv);
    float roughness = SampleLooseChannel(m.sources[5], s, uv) * m.roughnessFactor;
    float metallic = SampleLooseChannel(m.sources[6], s, uv) * m.metallicFactor;
    return float3(ao, roughness, metallic);
}

public float2 MaterialNormalXY(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    return float2(
        SampleLooseChannel(m.sources[7], s, uv), // X
        SampleLooseChannel(m.sources[8], s, uv)
    ); // Y
}
