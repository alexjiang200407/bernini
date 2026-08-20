import TextureHandle;

public struct PbrMaterial
{
    public float4 baseColorFactor;

    public float4 specular;

    public TextureHandle baseColorTexture; // SRGB
    public TextureHandle normalTexture;    // BC5_UNORM
    public TextureHandle ormTexture;       // UNORM

    public float metallicFactor;
    public float roughnessFactor;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // Only read by the alpha-test PSOs
    public float alphaCutoff;
};
