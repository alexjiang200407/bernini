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

    public uint channels[9];

    // Cutout threshold. Only read by the alpha-test PSOs; an opaque material never samples it.
    //
    // Sits here because 108 bytes of handles and channels leave the next float4 at 108 where MSL
    // wants 112: this fills that gap, and moving it below baseColorFactor reopens it and idlgen
    // refuses the struct.
    public float alphaCutoff;

    public float4 baseColorFactor;

    public float4 specular;
    public float metallicFactor;
    public float roughnessFactor;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;
};
