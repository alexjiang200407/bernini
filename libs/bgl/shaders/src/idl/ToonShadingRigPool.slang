// Where a placement's draws find its evaluated block: MeshInstance::flags carries the block's index
// plus one in these bits, zero on every placement not evaluated this draw -- which is every placement
// with no rig, since only the evaluation pass writes them. The low bits are MeshInstanceFlag's.
public static const uint cToonShadingRigSlotShift = 16;
public static const uint cToonShadingRigSlotMask = 0x7FF;

// A view's per-draw pool of evaluated blocks: how many rigged placements were selected. Zeroed per
// draw. It counts every placement that asked, so it may pass the pool's capacity; every reader clamps.
public struct ToonShadingRigPool
{
    public Atomic<uint> selected;
};
