import clip_playback;
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.Rig;
import idl.SkinnedBone;
import idl.SkinnedState;
import pose_walk;
import types.ComputeBuffer;
import types.EntryBuffer;
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
    // The SkinnedState indices to pose, one per workgroup. Dense: erasing a state slot only releases
    // it, so a sweep of the state buffer would pose freed instances.
    UploadBuffer<uint> posedInstances;

    EntryBuffer<SkinnedState> skinnedStates;
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
    let stateIdx = gUniforms.posedInstances[groupIdx];
    SkinnedState state = gUniforms.skinnedStates.Get(stateIdx);
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
