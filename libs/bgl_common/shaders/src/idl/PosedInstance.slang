import FootIKLeg;
import Range;

// One entry of a view's pose list: the placement to pose, and its foot-IK record -- one FootIKLeg
// per leg of the rig, in leg order. Null on a rig that authored no legs.
//
// The record is reached from here and not from the placement or the playback record because the
// pose pass is its only reader, and this list is that pass's own input.
public struct PosedInstance
{
    public uint mesh;
    public Range<FootIKLeg> footIK;
};
