import ToonShadingRig;
import ToonShadingRigLimits;

// One evaluated edit: its keys blended for this frame's light, with the frame it is measured in
// built. Head space.
public struct ToonShadingRigSlot
{
    public float4 positionAndGain;
    public float4 axisXAndSize;
    public float4 axisYAndAnisotropy;
    public float4 axisZAndSharpness;
    public float4 bendBulgeRotation;     // bend, bulge, cos(rotation), sin(rotation)
    public float4 radiusSmoothingMirror; // radius, normalSmoothing, 1 on a mirrored twin else 0, unused
};

// What one rigged placement's face pixels read this frame: written by the evaluation pass after the
// pose pass, read by the forward pixel stage.
public struct ToonShadingRigBlock
{
    // World space to head space: the rows of an affine matrix.
    public float4 headFromWorld[3];

    // xyz the unit world-space direction toward the remapped face light; w how much of the edits
    // the head's projected size leaves, in [0, 1].
    public float4 faceLight;

    // The rig's ToonShadingRig::faceEllipsoid, head space.
    public float4 faceEllipsoid;

    public uint slotCount;

    // The MeshInstance entry the block was evaluated for.
    public uint placement;
    public uint pad[2];

    public ToonShadingRigSlot slots[cMaxToonShadingRigSlots];
};
