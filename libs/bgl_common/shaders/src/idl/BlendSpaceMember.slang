// One clip of a blend space, resolved against the rig's clip table, and the parameter value at
// which it plays alone. A space's members are a contiguous run in strictly increasing parameter
// order, so the pair straddling a parameter is found by one walk.
public struct BlendSpaceMember
{
    public uint clip;
    public float parameter;
};
