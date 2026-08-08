// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY

public struct TextureHandle
{
    public Texture2D.Handle texture;

    public float4 Sample(SamplerState.Handle s, float2 uv) { return texture.Sample(s, uv); }

    public float4 SampleLevel(SamplerState.Handle s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }

    // Mip 0 texel counts. A caller relating a UV footprint to texels needs the real size;
    // hardcoding one would silently mis-scale the moment the asset changed.
    public float2 GetDimensions()
    {
        uint w;
        uint h;
        uint levels;
        texture.GetDimensions(0, w, h, levels);
        return float2(w, h);
    }
};

public struct TextureCubeHandle
{
    public TextureCube.Handle texture;

    public float4 SampleCube(SamplerState.Handle s, float3 uvw) { return texture.Sample(s, uvw); }

    public float4 SampleCubeLevel(SamplerState.Handle s, float3 uvw, float level) { return texture.SampleLevel(s, uvw, level); }

    // Texels along one face edge of mip 0. A caller picking its own LOD needs this to know what mip 0
    // already resolves; hardcoding it would silently mis-scale the moment the asset changed size.
    public uint CubeFaceTexels()
    {
        uint w;
        uint h;
        uint levels;
        texture.GetDimensions(0, w, h, levels);
        return w;
    }
};
