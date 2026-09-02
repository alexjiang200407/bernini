// THIS IS A FILE GENERATED FROM InstanceVisibility.slang. DO NOT EDIT MANUALLY

public struct InstanceVisibility
{
    // 0 culled, 1 visible. A one-word slot, not a packed bit, because the roadmap widens it: an LOD
    // index, an occlusion-phase bit (C4), an animation-tick tag.
    public uint visible;
}
