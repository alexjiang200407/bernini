import TextureHandle;

// Widest member first: WGSL aligns a vector to its own width where the scalar layout the CPU mirror
// uses packs it tight, so a float4 after the handles would sit at a different offset on each.
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
