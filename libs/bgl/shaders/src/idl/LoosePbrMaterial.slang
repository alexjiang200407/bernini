// THIS IS A FILE GENERATED FROM LoosePbrMaterial.slang. DO NOT EDIT MANUALLY

import ChannelSource;

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
    public float4 baseColorFactor;

    public float4 specular;
    public ChannelSource sources[9];
    public float metallicFactor;
    public float roughnessFactor;

    // What baseColorFactor.a means on a blended surface: 0 for coverage, 1 for transmission. Only
    // read by the transparent PSO. See MaterialData::ShadeBlended.
    public float transmissionFactor;

    // Cutout threshold. Only read by the alpha-test PSOs; an opaque material never samples it.
    public float alphaCutoff;
};
