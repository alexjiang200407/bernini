import Entry;
import TextureHandle;

public struct PbrMaterial
{
    public float4 baseColorFactor;

    public float4 specular;

    // Into the scene's texture table, not handles inline: a payload holding a resource handle
    // cannot be read out of a raw buffer at all on Metal, and this one is going into one. See
    // docs/slang_shaders.md.
    public Entry<TextureHandle> baseColorTexture; // SRGB
    public Entry<TextureHandle> normalTexture;    // BC5_UNORM
    public Entry<TextureHandle> ormTexture;       // UNORM

    public float metallicFactor;
    public float roughnessFactor;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // Only read by the alpha-test PSOs
    public float alphaCutoff;
};
