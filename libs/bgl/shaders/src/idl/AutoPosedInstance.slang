import FootIKLeg;
import Range;

// One entry of a view's automatic pose list: an automatic placement the camera's cull drew per
// instance this frame, the pose-pool slice it cut for it, and how far its foot IK reaches. Written
// by the cull, one per placement drawn per instance, and read by the pose pass's second dispatch,
// whose group count the same cull counted.
//
// The slice and the IK scale travel here rather than being looked up because the pose pass runs in
// the view's scope and the cull's words -- where the dissolve that sets the scale lives -- are one
// scope in, out of its reach.
public struct AutoPosedInstance
{
    public uint meshInstanceIndex;

    // One FootIKLeg per leg of the rig, in leg order; null on a rig that authored no legs.
    public Range<FootIKLeg> footIK;

    // Laid out as SkinnedState::palette is: the pose at `time`, the pose at `prevTime`, then one
    // sole per leg.
    public Range<float4> palette;

    // Multiplies both of the record's IK weights: 1 while the placement draws only per instance,
    // falling to 0 across a dissolve to its table so the feet reach their unplanted pose as the
    // table takes over, and rising back on the way in.
    public float ikScale;
};
