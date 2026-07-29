// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY

import SamplerHandle;

// Lives inside structs the CPU uploads into GPU buffers (the material tables), where each texture
// is an 8-byte descriptor handle -- the same bytes on every backend, which StrideProbe_test pins.
// WGSL has no bindless and a storage buffer cannot contain a texture object, so there the field
// stays a uint2: the handle data still round-trips, it just cannot be dereferenced. The sample
// methods are omitted on that target so a use is a compile error rather than a wrong bind; the W4
// material atlas is what makes materials sampleable on WebGPU.
public struct TextureHandle
{
#if defined(BGL_WGSL)
    public uint2 texture;
#else
    public Texture2D.Handle texture;

    public float4 Sample(SamplerHandle s, float2 uv) { return texture.Sample(s, uv); }

    public float4 SampleLevel(SamplerHandle s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }
#endif
};

// Constant-buffer-resident like TextureCubeHandle below (the BRDF LUT), so it stays a real,
// sampleable texture on every target -- what the buffer-resident TextureHandle above cannot be.
public struct Texture2DHandle
{
#if defined(BGL_WGSL)
    public Texture2D texture;
#else
    public Texture2D.Handle texture;
#endif

    public float4 Sample(SamplerHandle s, float2 uv) { return texture.Sample(s, uv); }

    public float4 SampleLevel(SamplerHandle s, float2 uv, float level) { return texture.SampleLevel(s, uv, level); }
};

// Only ever constant-buffer-resident (skybox, IBL maps), where Slang hoists the WGSL form to its
// own binding -- so unlike TextureHandle this stays a real texture on every target.
public struct TextureCubeHandle
{
#if defined(BGL_WGSL)
    public TextureCube texture;
#else
    public TextureCube.Handle texture;
#endif

    public float4 SampleCube(SamplerHandle s, float3 uvw) { return texture.Sample(s, uvw); }

    public float4 SampleCubeLevel(SamplerHandle s, float3 uvw, float level) { return texture.SampleLevel(s, uvw, level); }

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
