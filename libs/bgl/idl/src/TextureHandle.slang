public struct TextureHandle
{
    public Texture2D.Handle texture;

    public float4 Sample(SamplerState.Handle s, float2 uv) { return texture.Sample(s, uv); }

    public float4 SampleLevel(SamplerState.Handle s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }
};

public struct TextureCubeHandle
{
    public TextureCube.Handle texture;

    public float4 SampleCube(SamplerState.Handle s, float3 uvw) { return texture.Sample(s, uvw); }

    public float4 SampleCubeLevel(SamplerState.Handle s, float3 uvw, float level) { return texture.SampleLevel(s, uvw, level); }
};
