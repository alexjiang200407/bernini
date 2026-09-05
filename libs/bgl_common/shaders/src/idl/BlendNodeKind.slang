// What a node of the rig's node table plays. A kind and not a member count of zero: a reader
// branches on this before it looks at anything else, and a count is an answer to a later question.
public enum BlendNodeKind : uint32_t
{
    kClip = 0,  // one clip, played directly
    kSpace,     // a run of members blended by the slot's parameter
    kCount,
}
;
