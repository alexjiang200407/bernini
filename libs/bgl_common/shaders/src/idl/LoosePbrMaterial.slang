import RawTextureHandle;

public enum PbrChannel
{
    kBaseColorR,
    kBaseColorG,
    kBaseColorB,
    kBaseColorA,
    kAo,
    kRoughness,
    kMetallic,
    kNormalX,
    kNormalY,
}
;

public static const uint cLooseChannelCount = 9;

public struct LoosePbrMaterial
{
    // Handles first and contiguous, as in PbrMaterial: a raw-loaded payload samples a texture
    // through the arena's typed view, indexed by its position rather than by an offset the shader
    // would have to be told. `channels` is parallel: channel i selects a component of texture i.
    public RawTextureHandle textures[9];

    // Sampled whole through the second UV set, never routed: handle 9, after the channels' nine.
    public RawTextureHandle geometryOcclusionTexture;

    public uint channels[9];

    // Cutout threshold. Only read by the alpha-test PSOs; an opaque material never samples it.
    public float alphaCutoff;

    // Ten handles, nine channels and the cutoff leave the next float4 at 120 where MSL wants 128:
    // these two fill that gap, and moving either below baseColorFactor reopens it and idlgen
    // refuses the struct.
    public float metallicFactor;
    public float roughnessFactor;

    public float4 baseColorFactor;

    public float4 specular;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // As PbrMaterial.doubleSided.
    public uint doubleSided;
};
