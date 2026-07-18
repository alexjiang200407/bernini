import SamplerHandle;

public struct TextureHandle
{
    public Texture2D.Handle texture;

    public float4 Sample(SamplerHandle s, float2 uv) { return texture.Sample(s, uv); }

    public float4 SampleLevel(SamplerHandle s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }
};

public struct TextureCubeHandle
{
    public TextureCube.Handle texture;

    public float4 SampleCube(SamplerHandle s, float3 uvw) { return texture.Sample(s, uvw); }

    public float4 SampleCubeLevel(SamplerHandle s, float3 uvw, float level) { return texture.SampleLevel(s, uvw, level); }
};
