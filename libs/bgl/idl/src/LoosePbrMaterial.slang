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
};

public static const uint cLooseChannelCount = 9;

public struct LoosePbrMaterial
{
    public ChannelSource sources[9];
    public float4 baseColorFactor;
    public float metallicFactor;
    public float roughnessFactor;
};
