// What one submesh instance draws this frame under one culled frustum, written by CullInstances:
// a word of the bits below. Zero is culled, hidden or padding. A word rather than a bit per slot
// because the roadmap widens it further: an occlusion-phase bit (C4), an animation-tick tag.
public struct InstanceVisibility
{
    public uint visible;
}

// The placement's current level draws.
public static const uint cVisibleCurrentBit = 1;

// The level the placement is fading out of draws too, dithered against the current one; the
// compaction emits a second entry for it, tagged cOutgoingDrawBit.
public static const uint cVisibleOutgoingBit = 2;
