// How a forward dispatch picks the level of detail each placement it draws is at
// (ExpansionData.lodDrawMode).
public enum LodDrawMode : uint32_t
{
    // The level the cull chose, swapped at once: a draw bucket's at-rest lane, the depth-sorted
    // list, since a blend has no depth to dither against, and the outline mask.
    kCurrent = 0,
    // The chosen level dithered against the one it is leaving: a draw bucket's dissolve lane.
    kDissolve,
}
