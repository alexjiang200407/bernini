// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY

// On WGSL the bindless .Handle forms do not exist; what replaces them depends on where the struct
// lives. idlgen never defines BGL_WGSL, so the generated C++ mirrors always carry the uint2 handle.
#if defined(BGL_WGSL)
public typealias SamplerHandle = SamplerState;
#else
public typealias SamplerHandle = SamplerState.Handle;
#endif

// Buffer-resident (the material tables), so the WGSL form must keep the uint2 footprint the CPU
// writes -- a storage buffer cannot hold a texture there. The sample methods are absent on WGSL so
// a use fails at compile time; sampling materials on WebGPU is the W4 atlas redesign.
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
