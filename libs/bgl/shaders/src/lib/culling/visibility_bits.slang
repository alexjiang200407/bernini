import idl.Geom;
import idl.InstanceLod;
import idl.InstanceVisibility;

// The visibility word of a frustum-tested instance from its placement's level-of-detail word: the
// level it draws, the one a dissolve is leaving, and whether either dissolves. Shared by both
// phases of the cull, so phase 2 draws exactly the entries phase 1 would have.
public uint VisibilityBits(InstanceLod word, Geom geom, bool visible)
{
    uint bits = 0u;
    if (visible && uint(word.Level()) < geom.submeshes.lodCount)
    {
        bits |= cVisibleCurrentBit;
    }
    if (visible && word.Fading() && uint(word.OutgoingLevel()) < geom.submeshes.lodCount)
    {
        bits |= cVisibleOutgoingBit;
    }
    if (bits != 0u && word.Fading())
    {
        bits |= cVisibleDissolvingBit;
    }
    return bits;
}
