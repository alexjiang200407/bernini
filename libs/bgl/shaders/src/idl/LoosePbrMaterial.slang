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

// Widest member first, for the reason given in PbrMaterial.
public struct LoosePbrMaterial
{
    public float4 baseColorFactor;
    public ChannelSource sources[9];
    public float metallicFactor;
    public float roughnessFactor;

    // Cutout threshold. Only read by the alpha-test PSOs; an opaque material never samples it.
    public float alphaCutoff;

    public uint _wgslPad[1];
};
