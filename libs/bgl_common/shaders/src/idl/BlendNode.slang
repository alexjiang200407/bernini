import BlendNodeKind;

// One node of a rig's node table: what a playback slot names. A clip node is one clip played
// directly; a space node is a run of members blended by the slot's parameter.
//
// The table is clip nodes first, one per clip and in clip order, then the authored spaces -- so a
// slot naming node `n` below `clipCount` plays clip `n`, which is what lets a one-clip spawn keep
// meaning what it did before blend spaces existed.
public struct BlendNode
{
    public BlendNodeKind kind;

    // kClip: the clip played. Unused by a space node, whose clips are its members'.
    public uint clip;

    // kSpace: the run in the rig's member table. `memberCount` is at least two.
    public uint firstMember;
    public uint memberCount;
};
