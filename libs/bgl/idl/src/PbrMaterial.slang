import TextureHandle;

public struct PbrMaterial
{
    public TextureHandle baseColorTexture;
    public TextureHandle normalTexture;
    public TextureHandle ormTexture;

    public float4 baseColorFactor;
    public float metallicFactor;
    public float roughnessFactor;
};
