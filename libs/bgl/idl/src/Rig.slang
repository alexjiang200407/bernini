import BoneSample;
import Clip;
import Range;
import RangeWithCount;
import SkinnedBone;

// One rig as the GPU sees it: the bone table a hierarchy walk reads, the clip table an instance
// indexes, and the sample pool both address.
//
// A rig belongs to no mesh. Every geom skinned to the same skeleton and clip set shares one of
// these, which is what lets a modular unit draw as several meshes on one pose.
public struct Rig
{
    public Range<SkinnedBone> bones;
    public Range<BoneSample> samples;
    public RangeWithCount<Clip> clips;

    // What makes a frame addressable: the pool is frame-major, so one frame is a contiguous run of
    // exactly this many samples. Equal to the bone table's length.
    public uint boneCount;

    // The deepest bone's `depth`. A walk needs the level count up front rather than discovering it.
    public uint maxDepth;

    // Every frame of every clip, posed once. Frame-major like `samples` and addressed the same way:
    // bone `b` of global frame `f` is at `GetStart() + (f * boneCount + b) * cFloat4sPerBone`. Null
    // until something asks for one, and PoseRigFrames is the only writer.
    public Range<float4> boneAnimTable;
};
