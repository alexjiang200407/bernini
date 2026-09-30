import LodLevel;

// The level of detail one placement draws under one culled frustum, and the dissolve it is in.
// Written by CullInstances each frame and read back by it the next, which is what hysteresis
// needs; indexed by the placement's MeshInstance entry, which holds still while the placement
// lives where the dense instance slot does not. One word: 90k placements is 360 KB per frustum.
//
// The packing is these constants, and the CPU decode (bgl::UnpackInstanceLod) reads the same ones.
public static const uint cInstanceLodLevelMask = 0xFF;
public static const uint cInstanceLodOutgoingShift = 8;
public static const uint cInstanceLodFadeShift = 16;
public static const float cInstanceLodFadeScale = 65535.0;

public struct InstanceLod
{
    // Bits 0-7 the current level plus one, zero being a placement no cull has chosen for yet;
    // bits 8-15 the outgoing level plus one, zero while not fading; bits 16-31 the fade's progress
    // toward the current level in 1/65535ths.
    public uint packed;

    public bool Unset() { return (packed & cInstanceLodLevelMask) == 0u; }

    // @pre !Unset().
    public LodLevel Level() { return LodLevel((packed & cInstanceLodLevelMask) - 1u); }

    public bool Fading() { return ((packed >> cInstanceLodOutgoingShift) & cInstanceLodLevelMask) != 0u; }

    // @pre Fading().
    public LodLevel OutgoingLevel() { return LodLevel(((packed >> cInstanceLodOutgoingShift) & cInstanceLodLevelMask) - 1u); }

    // 1 once the dissolve is over, and on a placement not fading.
    public float Fade() { return Fading() ? float(packed >> cInstanceLodFadeShift) / cInstanceLodFadeScale : 1.0; }

    // @pre fade in [0, 1].
    public static InstanceLod Make(LodLevel level, bool fading, LodLevel outgoing, float fade)
    {
        InstanceLod word;
        word.packed = (uint(level) + 1u) & cInstanceLodLevelMask;
        if (fading)
        {
            word.packed |= ((uint(outgoing) + 1u) & cInstanceLodLevelMask) << cInstanceLodOutgoingShift;
            word.packed |= uint(round(saturate(fade) * cInstanceLodFadeScale)) << cInstanceLodFadeShift;
        }
        return word;
    }
};
