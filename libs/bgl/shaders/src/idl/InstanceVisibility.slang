import DrawBucket;

// What one submesh instance draws this frame under one culled frustum, written by CullInstances:
// a word of the bits below. Zero is culled, hidden or padding. A word rather than a bit per slot
// because the roadmap widens it further: an occlusion-phase bit (C4), an animation-tick tag.
public struct InstanceVisibility
{
    public uint visible;

    // The draw lane its entries count and compact into: the bucket's own, or its dissolve lane.
    // The histogram and the compaction both ask here, so the two cannot place an entry apart.
    public uint Lane(uint drawBucket) { return (visible & cVisibleDissolvingBit) != 0u ? drawBucket + cDissolveLane : drawBucket; }
}

// The placement's current level draws.
public static const uint cVisibleCurrentBit = 1;

// The level the placement is fading out of draws too, dithered against the current one; the
// compaction emits a second entry for it, tagged cOutgoingDrawBit.
public static const uint cVisibleOutgoingBit = 2;

// The placement is dissolving between two levels: its entries draw through its bucket's dissolve
// lane (cDissolveLane), whose pipelines carry the dither, so a placement at rest never pays for it.
public static const uint cVisibleDissolvingBit = 4;

// The placement's impostor draws: it is past its geom's last level, or dissolving into or out of
// that tier. Set on the placement's first submesh instance only, since an impostor is one quad a
// placement, not one a submesh, and counted into cImpostorDrawBucket's lanes rather than the
// instance's own bucket.
public static const uint cVisibleImpostorBit = 8;
