// How a forward dispatch picks the level of detail each placement it draws is at
// (ExpansionData.lodDrawMode).
public enum LodDrawMode : uint32_t
{
    // Level 0 whatever the cull chose: a pass that binds no cull output, such as the outline mask.
    kLevel0 = 0,
    // The level the cull chose, swapped at once: a draw bucket's at-rest lane, and the depth-sorted
    // list, since a blend has no depth to dither against.
    kCurrent,
    // The chosen level dithered against the one it is leaving: a draw bucket's dissolve lane.
    kDissolve,
}
