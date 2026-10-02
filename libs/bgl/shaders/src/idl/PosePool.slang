// A view's per-frame pose pool, for the placements on the automatic source. Its three counters
// are zeroed per draw; `capacity` is written after them.
//
// The budget (CullView::poseBudget) counts placements with an entry drawn per instance, a dissolve's
// outgoing entry included, and is granted a frame ahead: a placement draws per instance only if it
// did last frame or was granted the right then, and the grants are at most the budget less what
// was drawn. So no frame draws more than the budget, and a placement that holds the right keeps it
// while it wants it.
public struct PosePool
{
    // Placements drawn per instance this frame. Each cut a slice and took an entry of the pose list,
    // so this is that list's length.
    public Atomic<uint> posed;

    // Placements that want to be drawn per instance and may not yet: the first `poseBudget - posed`
    // of them, in the order they asked, are granted it for next frame.
    public Atomic<uint> requested;

    // float4s of the pool handed out this frame, and the capacity they are cut from.
    public Atomic<uint> cursor;
    public uint capacity;
};
