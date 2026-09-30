// One bone's local pose in one frame of a clip, relative to its parent.
//
// Three float4s rather than a float3/quat/float3: a float3 field has no layout every backend agrees
// on, so a C++-mirrored struct may not carry one (see docs/idlgen.md). The w components are padding.
// This is why a rig's samples are converted on upload instead of memcpy'd -- assetlib's Transform
// packs the same three values into 40 bytes.
public struct BoneSample
{
    public float4 translation;
    public float4 rotation; // quaternion, xyzw
    public float4 scale;
};
