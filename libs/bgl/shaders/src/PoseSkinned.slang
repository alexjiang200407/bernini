import clip_playback;
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.Rig;
import idl.SkinnedBone;
import idl.SkinnedState;
import idl.IPlayback;
import idl.RawEntry;
import pose_walk;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.RawBuffer;
import types.RangeBuffer;
import types.UploadBuffer;

// One workgroup poses one skinned instance: sample its clip, walk the hierarchy, multiply through the
// inverse bind, write the palette. Twice over -- once at `time` and once at `prevTime` -- so the mesh
// shader can skin both and derive a motion vector without a history buffer.
//
// The walk itself is pose_walk's, shared with PoseRigFrames. What is this kernel's own is where a
// pose comes from: an instance's clip, phase and rate against the frame's clock.

struct Uniforms
{
    // The byte offsets of the playback records to pose, one per workgroup. Dense: erasing a record
    // only releases its bytes, so a sweep of the arena would pose freed instances -- and would meet
    // the VAT records sharing it, which this pass has nothing to do with.
    UploadBuffer<uint> posedInstances;

    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;

    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BoneSample> sampleBuffer;

    ComputeBuffer<float4> bonePalettes;

    float time;
    float prevTime;
    uint posedCount;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cPoseGroupSize, 1, 1)]
void main(uint groupIdx: SV_GroupID, uint localIdx: SV_GroupIndex)
{
    if (groupIdx >= gUniforms.posedCount)
    {
        return;
    }

    // Group-uniform, so the early-out above and every barrier below are reached by the whole group
    // or by none of it.
    let byteOffset = gUniforms.posedInstances[groupIdx];
    SkinnedState state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedState>(RawEntry<IPlayback>.At(byteOffset));
    Rig rig = gUniforms.rigs.Get(state.rig);
    Clip clip = gUniforms.clipBuffer.Get(rig.clips, state.clip);

    PoseTables tables;
    tables.bones = gUniforms.boneBuffer;
    tables.samples = gUniforms.sampleBuffer;
    tables.palettes = gUniforms.bonePalettes;

    let stride = rig.boneCount * cFloat4sPerBone;

    let base = state.palette.GetStart();

    PoseInto(tables, rig, clip, ClipFrames(clip, state.phase, state.rate, gUniforms.time), base, localIdx);
    PoseInto(tables, rig, clip, ClipFrames(clip, state.phase, state.rate, gUniforms.prevTime), base + stride, localIdx);
}
