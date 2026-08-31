// THIS IS A FILE GENERATED FROM SkinnedLegChain.slang. DO NOT EDIT MANUALLY

// One leg of a rig, resolved to bone indices against the skeleton it was uploaded with, and the
// plane its sole rests on.
//
// The chain is direct: `knee`'s parent is `hip`, `ankle`'s is `knee`, `toe`'s is `ankle`. The solve
// rewrites those four slots and carries their descendants rigidly, so a bone between two links
// would be left holding a pose the joints above it no longer agree with.
public struct SkinnedLegChain
{
    // Ankle-local: the sole's contact plane, fitted to the underside of the vertices weighted to the
    // ankle and the toe at bind pose. `w` is padding -- a float3 field has no layout every backend
    // agrees on (docs/idlgen.md).
    public float4 solePoint;
    public float4 soleNormal;

    public uint hip;
    public uint knee;
    public uint ankle;
    public uint toe;
};
