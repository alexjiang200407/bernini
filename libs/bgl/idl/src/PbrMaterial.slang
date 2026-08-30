import RawTextureHandle;

public struct PbrMaterial
{
    // Handles first, and contiguous: a raw-loaded payload stores a texture as bytes and samples it
    // through the arena's typed view, which is indexed by (payloadOffset + i * 8) / 8. Their
    // position is what the shader knows them by, so nothing may be inserted above or between them.
    public RawTextureHandle baseColorTexture; // SRGB
    public RawTextureHandle normalTexture;    // BC5_UNORM
    public RawTextureHandle ormTexture;       // UNORM

    // Three eight-byte handles leave the next float4 at 24, where MSL wants it at 32. These two fill
    // that gap, so moving either below baseColorFactor reopens it and idlgen refuses the struct.
    public float metallicFactor;
    public float roughnessFactor;

    public float4 baseColorFactor;

    public float4 specular;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // Only read by the alpha-test PSOs
    public float alphaCutoff;
};
