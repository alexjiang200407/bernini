import idl.LoosePbrMaterial;
import idl.PbrMaterial;
import idl.SamplerHandle;

// The factor-only material reads: the WGSL arm of the pair MaterialData.slang imports one of,
// chosen by target. A material-table texture is a uint2 there, with nothing to dereference it
// against until the W4 atlas, so every read collapses to its factor; (0.5, 0.5) decodes to the
// flat normal. MaterialSampled.slang is the other arm; the two export identical signatures.

public float4 MaterialBaseColor(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return m.baseColorFactor;
}

public float3 MaterialOrm(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return float3(1.0, m.roughnessFactor, m.metallicFactor);
}

public float2 MaterialNormalXY(PbrMaterial m, SamplerHandle s, float2 uv)
{
    return float2(0.5, 0.5);
}

public float4 MaterialBaseColor(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    return m.baseColorFactor;
}

public float3 MaterialOrm(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    return float3(1.0, m.roughnessFactor, m.metallicFactor);
}

public float2 MaterialNormalXY(LoosePbrMaterial m, SamplerHandle s, float2 uv)
{
    return float2(0.5, 0.5);
}
