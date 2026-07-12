// THIS IS A FILE GENERATED FROM PbrMaterial.slang. DO NOT EDIT MANUALLY

import TextureHandle;

public struct PbrMaterial
{
    public TextureHandle baseColorTexture; // SRGB
    public TextureHandle normalTexture;    // BC5_UNORM
    public TextureHandle ormTexture;       // UNORM

    public float4 baseColorFactor;
    public float metallicFactor;
    public float roughnessFactor;

    // Cutout threshold. Only read by the alpha-test PSOs; an opaque material never samples it.
    public float alphaCutoff;
};
