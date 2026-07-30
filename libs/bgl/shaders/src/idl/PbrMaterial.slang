// THIS IS A FILE GENERATED FROM PbrMaterial.slang. DO NOT EDIT MANUALLY

import TextureHandle;

public struct PbrMaterial
{
    public float4 baseColorFactor;

    public TextureHandle baseColorTexture; // SRGB
    public TextureHandle normalTexture;    // BC5_UNORM
    public TextureHandle ormTexture;       // UNORM

    public float metallicFactor;
    public float roughnessFactor;

    // Only read by the alpha-test PSOs
    public float alphaCutoff;
};
