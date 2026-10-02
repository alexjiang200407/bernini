import BlendSlot;
import Constants;
import Entry;
import FootIKLeg;
import Range;
import Rig;

// An automatic instance's playback record: SkinnedState's rig and slots, its foot-IK record, and no
// palette. The cull decides each frame whether the instance draws per instance -- posed into a
// slice of the view's pose pool that lives a frame, InstancePose -- or from its rig's table on its
// dominant slot, and InstanceLod's source bits say which each drawn entry takes. The foot-IK record
// is reached from here rather than from the pose list because the cull, not the CPU, writes that
// list for these instances.
public struct SkinnedAutoState
{
    public Entry<Rig> rig;
    public BlendSlot slots[cBlendSlots];

    // One FootIKLeg per leg of the rig, in leg order; null on a rig that authored no legs.
    public Range<FootIKLeg> footIK;
};
