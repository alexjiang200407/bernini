/**
 * An instance block as its writer sees it: `Capacity()` slots, each placed or hidden every frame.
 * The renderer implements it over the view's placements; a writer never learns where they live.
 */
public interface IMeshInstanceBlock
{
    uint Capacity();

    /**
     * Shows `slot` this frame at `transform`, drawn last frame at `prevTransform` -- the pair every
     * motion vector is taken from, so a slot that did not move passes the same matrix twice, and one
     * appearing this frame does too. Affine model-to-world matrices; the fourth row is discarded.
     */
    void Place(uint slot, float4x4 transform, float4x4 prevTransform);

    /** Culls `slot` this frame, before anything reads its transform. */
    void Hide(uint slot);

    /**
     * Plays `slot`'s animation `seconds` ahead of the view's clock, kept until set again: a block of
     * a skinned geom shares one playback record (MeshInstanceBlockDesc::playback), and this is what
     * a slot plays of it. Nothing reads it on a static geom's block.
     *
     * The pose a frame reprojects from is evaluated on the offset the slot holds now, so give a slot
     * the offset of whatever it draws this frame, and never change that thing's offset while it
     * lives: a new value is a jump in the pose that no motion vector describes.
     */
    void SetPlaybackOffset(uint slot, float seconds);
};

/**
 * What a caller's instance writer conforms to: parameters the caller sets per block from the CPU,
 * and a function the renderer calls once per slot, every frame, before the cull. `Write` places
 * or hides `slot`, every call: a slot left alone repeats whatever it was last given, motion included.
 */
public interface IMeshInstanceWriter
{
    /**
     * Everything the writer reads: buffers it imported, counts, a blend factor. Declared by the
     * writer and never filled by it; the renderer reflects it and the caller writes it by name
     * (ISceneView::GetBlockParams).
     */
    associatedtype Params;

    static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot);
};
