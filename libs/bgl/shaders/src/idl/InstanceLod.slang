import LodLevel;

// The level of detail one placement draws under one culled frustum, the dissolve it is in, and --
// for a placement on the automatic pose source -- which source each of its two entries draws from.
// Written by CullInstances each frame and read back by it the next, which is what hysteresis and
// the pose budget's grants need; indexed by the placement's MeshInstance entry, which holds still
// while the placement lives where the dense instance slot does not. One word: 90k placements is
// 360 KB per frustum.
//
// A level equal to the geom's lodCount is the tier past its last level: its impostor where the
// geom has one (Geom.impostor), and nothing drawn where it has none.
//
// The packing is these constants, and the CPU decode (bgl::UnpackInstanceLod) reads the same ones.
public static const uint cInstanceLodLevelMask = 0x0F;
public static const uint cInstanceLodOutgoingShift = 8;
public static const uint cInstanceLodFadeShift = 16;
public static const float cInstanceLodFadeScale = 65535.0;

// In the current byte and, shifted by cInstanceLodOutgoingShift, the outgoing one: that entry draws
// from its rig's bone anim table rather than per instance. Zero on every placement not on the
// automatic source, whose record alone says where its pose comes from.
public static const uint cInstanceLodTableBit = 0x80;

// The placement may draw per instance next frame: granted by this frame's pose pool. Only in the
// current byte.
public static const uint cInstanceLodGrantBit = 0x40;

public struct InstanceLod
{
    // Bits 0-3 the current level plus one, zero being a placement no cull has chosen for yet; bits
    // 8-11 the outgoing level plus one, zero while not fading; bits 16-31 the fade's progress toward
    // the current entry in 1/65535ths. cInstanceLodTableBit and cInstanceLodGrantBit sit in the
    // bytes' high bits. A fade may hold one level on both entries, when only the source changes.
    public uint packed;

    public bool Unset() { return (packed & cInstanceLodLevelMask) == 0u; }

    // @pre !Unset().
    public LodLevel Level() { return LodLevel((packed & cInstanceLodLevelMask) - 1u); }

    public bool Fading() { return ((packed >> cInstanceLodOutgoingShift) & cInstanceLodLevelMask) != 0u; }

    // @pre Fading().
    public LodLevel OutgoingLevel() { return LodLevel(((packed >> cInstanceLodOutgoingShift) & cInstanceLodLevelMask) - 1u); }

    // 1 once the dissolve is over, and on a placement not fading.
    public float Fade() { return Fading() ? float(packed >> cInstanceLodFadeShift) / cInstanceLodFadeScale : 1.0; }

    // @pre the placement's record is a SkinnedAutoState; on any other the bit is zero and says nothing.
    public bool FromTable() { return (packed & cInstanceLodTableBit) != 0u; }

    // @pre Fading(), and the record is a SkinnedAutoState.
    public bool OutgoingFromTable() { return ((packed >> cInstanceLodOutgoingShift) & cInstanceLodTableBit) != 0u; }

    // Whether either entry drawn this frame is posed per instance.
    // @pre !Unset(), and the record is a SkinnedAutoState.
    public bool DrawsPerInstance() { return !FromTable() || (Fading() && !OutgoingFromTable()); }

    public bool Granted() { return (packed & cInstanceLodGrantBit) != 0u; }

    // @pre fade in [0, 1].
    public static InstanceLod Make(LodLevel level, bool fading, LodLevel outgoing, float fade)
    {
        return MakeWithSources(level, false, fading, outgoing, false, fade);
    }

    // @pre fade in [0, 1].
    public static InstanceLod MakeWithSources(
        LodLevel level,
        bool fromTable,
        bool fading,
        LodLevel outgoing,
        bool outgoingFromTable,
        float fade
    )
    {
        InstanceLod word;
        word.packed = (uint(level) + 1u) & cInstanceLodLevelMask;
        if (fromTable)
        {
            word.packed |= cInstanceLodTableBit;
        }
        if (fading)
        {
            var byte = (uint(outgoing) + 1u) & cInstanceLodLevelMask;
            if (outgoingFromTable)
            {
                byte |= cInstanceLodTableBit;
            }
            word.packed |= byte << cInstanceLodOutgoingShift;
            word.packed |= uint(round(saturate(fade) * cInstanceLodFadeScale)) << cInstanceLodFadeShift;
        }
        return word;
    }

    public InstanceLod WithGrant()
    {
        InstanceLod word;
        word.packed = packed | cInstanceLodGrantBit;
        return word;
    }
};
