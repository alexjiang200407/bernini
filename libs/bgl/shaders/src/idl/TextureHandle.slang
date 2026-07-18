// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY

import SamplerHandle;

public struct TextureHandle
{
    public Texture2D.Handle texture;

    public float4 Sample(SamplerHandle s, float2 uv) { return texture.Sample(s.sampler, uv); }

    public float4 SampleLevel(SamplerHandle s, float2 uv, float level) { return texture.SampleLevel(s.sampler, uv, level); }
};

public struct TextureCubeHandle
{
    public TextureCube.Handle texture;

    public float4 SampleCube(SamplerHandle s, float3 uvw) { return texture.Sample(s.sampler, uvw); }

    public float4 SampleCubeLevel(SamplerHandle s, float3 uvw, float level) { return texture.SampleLevel(s.sampler, uvw, level); }
};
