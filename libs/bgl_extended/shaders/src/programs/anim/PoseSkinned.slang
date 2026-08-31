import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPlayback;
import idl.MeshInstance;
import idl.RawEntry;
import idl.Rig;
import idl.SkinnedBone;
import idl.SkinnedState;
import lib.anim.clip_playback;
import lib.anim.pose_walk;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One workgroup poses one skinned instance: sample its clip, walk the hierarchy, multiply through the
// inverse bind, write the palette. Twice over -- once at `time` and once at `prevTime` -- so the mesh
// shader can skin both and derive a motion vector without a history buffer.
//
// The walk itself is pose_walk's, shared with PoseRigFrames. What is this kernel's own is where a
// pose comes from: an instance's clip, phase and rate against the frame's clock.

struct Uniforms
{
    // The mesh instances to pose, one per workgroup. Dense: erasing a placement only releases its
    // bytes, so a sweep of the playback arena would pose freed instances -- and would meet the crowd
    // records sharing it, which own no palette for this pass to write.
    UploadBuffer<uint> posedInstances;

    // Where an instance stands and which record it plays from. Both are read from the placement
    // rather than copied into the playback record: a foot is planted against the ground under the
    // instance in the world, and one transform in two records is one that can disagree with itself.
    EntryBuffer<MeshInstance> meshBuffer;

    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;

    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BoneSample> sampleBuffer;

    ComputeBuffer<float4> bonePalettes;

    float time;
    float prevTime;
    uint posedCount;

    // The scene's ground plane, world space, normal unit length: what a planted foot is solved
    // against.
    float3 groundPoint;
    float3 groundNormal;
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
    let mesh = gUniforms.meshBuffer.Get(gUniforms.posedInstances[groupIdx]);
    SkinnedState state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedState>(mesh.playback);
    Rig rig = gUniforms.rigs.Get(state.playback.rig);
    Clip clip = gUniforms.clipBuffer.Get(rig.clips, state.playback.clip);

    PoseTables tables;
    tables.bones = gUniforms.boneBuffer;
    tables.samples = gUniforms.sampleBuffer;
    tables.palettes = gUniforms.bonePalettes;

    let stride = rig.boneCount * cFloat4sPerBone;

    let base = state.palette.GetStart();

    PoseInto(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.time), base, localIdx);
    PoseInto(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.prevTime), base + stride, localIdx);
}
