// WGSL-lowering driver for lib.anim.pose_walk, and the first IPoseTables built with no bindless
// handle: three storage buffers are the whole implementation. The group is cPoseGroupSize wide
// because PoseInto's barriers must be reached by every thread of it.
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPoseTables;
import idl.Range;
import idl.Rig;
import idl.SkinnedBone;
import lib.anim.pose_walk;

StructuredBuffer<SkinnedBone> gBones;
StructuredBuffer<BoneSample> gSamples;
RWStructuredBuffer<float4> gPalettes;

struct StorageTables : IPoseTables
{
    SkinnedBone GetBone(Range<SkinnedBone> bones, uint index) { return gBones[bones.GetStart() + index]; }
    BoneSample GetSample(Range<BoneSample> samples, uint index) { return gSamples[samples.GetStart() + index]; }
    float4 GetPalette(uint slot) { return gPalettes[slot]; }
    void SetPalette(uint slot, float4 value) { gPalettes[slot] = value; }
};

[shader("compute")]
[numthreads(cPoseGroupSize, 1, 1)]
void main(uint localIdx: SV_GroupIndex)
{
    StorageTables tables;
    Rig rig = {};
    Clip clip = {};
    LocalPose pose = SingleClipPose(clip, 0.0);
    PoseInto(tables, rig, pose, 0, localIdx);
    let sample = SampleLocal(tables, rig, pose, 0);
    WritePalette(tables, 0, mul(LocalTransform(sample), PaletteMatrix(tables, 0)));
}
