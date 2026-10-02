import Range;

// Where an automatic placement's per-instance pose sits this frame: a slice of the view's pose
// pool laid out as SkinnedState::palette is -- the pose at `time`, the pose at `prevTime`, then one
// sole per leg -- or null when no entry of the placement draws per instance. Indexed by the
// placement's MeshInstance entry and rewritten by every cull, because a pose is a function of the
// clock alone and needs no slice that survives a frame.
public struct InstancePose
{
    public Range<float4> palette;
};
