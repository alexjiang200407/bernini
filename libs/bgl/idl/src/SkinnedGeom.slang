import BoneSample;
import Range;
import RangeWithCount;
import SkinnedBone;
import SkinnedClip;

// One rig as the GPU sees it: the bone table a hierarchy walk reads, the clip table an instance
// indexes, and the sample pool both address. Unlike VatGeom this carries no textures -- a skinned
// pose is computed, not fetched.
public struct SkinnedGeom
{
    public Range<SkinnedBone> bones;
    public Range<BoneSample> samples;
    public RangeWithCount<SkinnedClip> clips;

    // What makes a frame addressable: the pool is frame-major, so one frame is a contiguous run of
    // exactly this many samples. Equal to the bone table's length.
    public uint boneCount;

    // The deepest bone's `depth`. A walk needs the level count up front rather than discovering it.
    public uint maxDepth;
};
