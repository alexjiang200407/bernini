import TextureHandle;

public struct PbrMaterial
{
    public TextureHandle baseColorTexture; // SRGB
    public TextureHandle normalTexture;    // BC5_UNORM
    public TextureHandle ormTexture;       // UNORM

    public float4 baseColorFactor;
    public float metallicFactor;
    public float roughnessFactor;
};
