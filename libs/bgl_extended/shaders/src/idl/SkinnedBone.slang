// THIS IS A FILE GENERATED FROM SkinnedBone.slang. DO NOT EDIT MANUALLY

// One bone of a rig, as the pose pass needs it.
//
// The bind pose is deliberately absent: a clip is resampled to carry a sample for every bone of
// every frame, so nothing ever falls back to a rest pose at runtime.
public struct SkinnedBone
{
    public float4x4 inverseBind; // model space -> bone space

    // Always lower than the bone's own index -- assetlib sorts bones topologically and validates it
    // at import -- so a local->model walk is a forward pass. cInvalidBone for a root.
    public uint parent;

    // Edges from the root. The walk solves one level at a time so the bones within a level can be
    // solved in parallel; derived at upload, because no container carries it.
    public uint depth;
};
