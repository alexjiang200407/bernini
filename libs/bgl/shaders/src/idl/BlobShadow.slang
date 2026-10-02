// One entry of a view's blob-shadow list: one disc, either the placement's own or one foot's, and
// how it falls. Dense and CPU-authored like the pose list -- the forward pass dispatches one group
// per entry.
public struct BlobShadow
{
    public uint mesh;
    public float radius;
    public float intensity;
    public float fadeHeight;

    // The body disc's BlobShadowDesc::casterLift, or a foot's FootShadowDesc::maxReceiverRise: the
    // two meet the same problem differently, see programs.forward.BlobShadow.
    public float lift;

    // Which leg of the placement's rig the foot is, or cBodyDisc on the placement's own disc. The
    // pass reads that leg's sole from whichever source the placement drew this frame: its palette,
    // its pose-pool slice, or its rig's table soles.
    public uint leg;
};
