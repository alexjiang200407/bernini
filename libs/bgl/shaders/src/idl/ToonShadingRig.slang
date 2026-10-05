import Range;
import ToonShadingRigLimits;

// ToonShadingRig::headBoneIndex on a rig whose head is the placement's own frame.
public static const uint cNoHeadBone = 0xFFFFFFFF;

// One bit of ToonShadingRigEdit::flags. A zeroed word is an edit drawn once whose blend may take
// either sign.
public enum ToonShadingRigEditFlag : uint32_t
{
    // The edit has a twin evaluated with head-space X flipped.
    kMirrored = 1,

    // Sign locks, derived on upload from the keys' gains: an edit whose gains are all of one sign
    // keeps it between keys, so a blend never turns a shadow into a light. At most one is set.
    kShadeOnly = 2,
    kLightOnly = 4,
}
;

// One key of an edit, head space: bgl::ToonShadingRigKeyDesc packed four floats to a row.
public struct ToonShadingRigKey
{
    public float4 lightAndGain;            // xyz the unit direction toward the light, w gain
    public float4 positionAndSize;         // xyz position, w size
    public float4 shape;                   // anisotropy, sharpness, bend, bulge
    public float4 rotationRadiusSmoothing; // rotation, radius, normalSmoothing, unused
};

// One edit: a run of the rig's keys and how they blend.
public struct ToonShadingRigEdit
{
    // Relative to ToonShadingRig::keys.
    public uint firstKey;
    public uint keyCount;
    public float keySharpness;
    public uint flags;

    public bool Has(ToonShadingRigEditFlag flag) { return (flags & uint(flag)) != 0u; }
};

// A face's toon shading rig as the evaluation pass reads it: bgl::ToonShadingRigDesc, flattened. One per
// AddToonShadingRig, shared by every placement that takes it.
public struct ToonShadingRig
{
    // Head space to the head bone's model space, or to the placement's: the rows of an affine
    // matrix.
    public float4 headToBone[3];

    public uint headBoneIndex;
    public uint editCount;
    // Edits plus mirrored twins: what the evaluated block holds.
    public uint slotCount;
    public float headRadius;

    public float fadeStartPixels;
    public float fadeEndPixels;
    public float minElevation;
    public float maxElevation;

    public float maxAzimuth;
    public float azimuthFadeStart;
    public float azimuthFadeEnd;
    public float azimuthFadeAmount;

    // The ellipsoid a face's base normal is pulled toward, about the head's origin: xyz each axis's
    // (smallest radius / radius) squared, the scale its gradient applies to a head-space point; w
    // how far the normal is pulled, in [0, 1].
    public float4 faceEllipsoid;

    public Range<ToonShadingRigKey> keys;
    public uint pad[3];

    public ToonShadingRigEdit edits[cMaxToonShadingRigSlots];
};
