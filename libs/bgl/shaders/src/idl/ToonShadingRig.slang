import Range;

// Slots one rig evaluates: an edit takes one, a mirrored edit two. bgl::c_MaxToonShadingRigSlots is the
// same number.
public static const uint cMaxToonShadingRigSlots = 8;

// ToonShadingRig::headBone on a rig whose head is the placement's own frame.
public static const uint cNoHeadBone = 0xFFFFFFFF;

// ToonShadingRigEdit::flags. The sign locks are derived on upload from the keys' gains: an edit whose
// gains are all of one sign keeps it between keys, so a blend never turns a shadow into a light.
public static const uint cToonShadingRigEditMirrored = 0x1;
public static const uint cToonShadingRigEditShadeOnly = 0x2;
public static const uint cToonShadingRigEditLightOnly = 0x4;

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
};

// A face's toon shading rig as the evaluation pass reads it: bgl::ToonShadingRigDesc, flattened. One per
// AddToonShadingRig, shared by every placement that takes it.
public struct ToonShadingRig
{
    // Head space to the head bone's model space, or to the placement's: the rows of an affine
    // matrix.
    public float4 headToBone[3];

    public uint headBone;
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

    public Range<ToonShadingRigKey> keys;
    public uint pad0;
    public uint pad1;
    public uint pad2;

    public ToonShadingRigEdit edits[cMaxToonShadingRigSlots];
};
