import RawTextureHandle;

public struct PbrMaterial
{
    // Handles first, and contiguous: a raw-loaded payload stores a texture as bytes and samples it
    // through the arena's typed view, which is indexed by (payloadOffset + i * 8) / 8. Their
    // position is what the shader knows them by, so nothing may be inserted above or between them.
    public RawTextureHandle baseColorTexture;         // SRGB
    public RawTextureHandle normalTexture;            // BC5_UNORM
    public RawTextureHandle ormTexture;               // UNORM
    public RawTextureHandle geometryOcclusionTexture; // UNORM, sampled through the second UV set

    // Four eight-byte handles end at 32, where MSL wants a float4, so the vectors follow them and
    // the scalars trail. A fifth handle would push both vectors off alignment and idlgen refuses it.
    public float4 baseColorFactor;

    public float4 specular;

    public float metallicFactor;
    public float roughnessFactor;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // Only read by the alpha-test PSOs
    public float alphaCutoff;

    // Non-zero draws the surface's back faces; the mesh stage culls them otherwise.
    public uint doubleSided;
};
