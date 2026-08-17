import clip_playback;
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.SkinnedBone;
import idl.SkinnedGeom;
import idl.SkinnedState;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.RangeBuffer;
import types.UploadBuffer;

// One workgroup poses one skinned instance: sample its clip, walk the hierarchy, multiply through the
// inverse bind, write the palette. Twice over -- once at `time` and once at `prevTime` -- so the mesh
// shader can skin both and derive a motion vector without a history buffer.

struct Uniforms
{
    // The SkinnedState indices to pose, one per workgroup. Dense: erasing a state slot only releases
    // it, so a sweep of the state buffer would pose freed instances.
    UploadBuffer<uint> posedInstances;

    EntryBuffer<SkinnedState> skinnedStates;
    EntryBuffer<SkinnedGeom> skinnedGeoms;

    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BoneSample> sampleBuffer;

    ComputeBuffer<float4> bonePalettes;

    float time;
    float prevTime;
    uint posedCount;
};

ConstantBuffer<Uniforms> gUniforms;

// Model-space transforms of the instance this group is posing, indexed rig-locally. float4x4 rather
// than the palette's packed three rows: every compose below goes through mul(), and paying 16 bytes a
// bone here buys not having a second matrix convention in the walk. See cMaxBonesPerRig.
groupshared float4x4 gModel[cMaxBonesPerRig];

// A local TRS as the matrix mul() expects: rows of the rotation scaled per axis, translation in .w.
float4x4 LocalTransform(BoneSample sample)
{
    float4 q = normalize(sample.rotation);
    float3 s = sample.scale.xyz;

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    float3 r0 = float3(1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy));
    float3 r1 = float3(2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx));
    float3 r2 = float3(2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy));

    return float4x4(
        float4(r0 * s, sample.translation.x),
        float4(r1 * s, sample.translation.y),
        float4(r2 * s, sample.translation.z),
        float4(0.0, 0.0, 0.0, 1.0)
    );
}

// Blended local pose of bone `bone` at a fractional frame. Rotation is nlerp'd with a hemisphere
// flip, not slerp: clips are resampled to a fixed rate, so the angle between adjacent frames is small
// enough that the error is invisible and the transcendentals are not worth paying per bone.
BoneSample SampleBone(SkinnedGeom geom, Clip clip, uint bone, float frames)
{
    uint f0;
    uint f1;
    float weight;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    let base = clip.firstFrame * geom.boneCount + bone;
    BoneSample a = gUniforms.sampleBuffer.Get(geom.samples, base + f0 * geom.boneCount);
    BoneSample b = gUniforms.sampleBuffer.Get(geom.samples, base + f1 * geom.boneCount);

    // Without the flip a pair straddling the hemisphere boundary interpolates the long way round,
    // which reads as the bone snapping through its own axis.
    float4 rotB = (dot(a.rotation, b.rotation) < 0.0) ? -b.rotation : b.rotation;

    BoneSample blended;
    blended.translation = lerp(a.translation, b.translation, weight);
    blended.rotation = normalize(lerp(a.rotation, rotB, weight));
    blended.scale = lerp(a.scale, b.scale, weight);
    return blended;
}

// Poses one instance into the palette starting at `paletteBase`. Every thread of the group must reach
// this, and reach every barrier in it, or the ones that do hang.
void PoseInto(SkinnedGeom geom, Clip clip, SkinnedState state, float time, uint paletteBase, uint localIdx)
{
    let frames = ClipFrames(clip, state.phase, state.rate, time);

    for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
    {
        gModel[bone] = LocalTransform(SampleBone(geom, clip, bone, frames));
    }
    GroupMemoryBarrierWithGroupSync();

    // One level at a time: a bone's parent is always a lower index, but a lower index is not
    // necessarily a shallower one, so composing in index order would read a parent this pass has not
    // composed yet. Every thread runs every level, including the ones with no bone at that depth,
    // because the barrier is group-wide.
    for (uint level = 1; level <= geom.maxDepth; ++level)
    {
        for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
        {
            SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);
            if (record.depth == level)
            {
                gModel[bone] = mul(gModel[record.parent], gModel[bone]);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);
        float4x4 skin = mul(gModel[bone], record.inverseBind);

        // Three rows, not four: the fourth of an affine transform is always (0,0,0,1), and the mesh
        // shader puts it back.
        let dst = paletteBase + bone * cFloat4sPerBone;
        gUniforms.bonePalettes[dst + 0] = skin[0];
        gUniforms.bonePalettes[dst + 1] = skin[1];
        gUniforms.bonePalettes[dst + 2] = skin[2];
    }
    GroupMemoryBarrierWithGroupSync();
}

[shader("compute")]
[numthreads(cPoseGroupSize, 1, 1)]
void main(uint groupIdx: SV_GroupID, uint localIdx: SV_GroupIndex)
{
    if (groupIdx >= gUniforms.posedCount)
    {
        return;
    }

    let stateIdx = gUniforms.posedInstances[groupIdx];
    SkinnedState state = gUniforms.skinnedStates.Get(stateIdx);
    SkinnedGeom geom = gUniforms.skinnedGeoms.Get(state.geom);
    Clip clip = gUniforms.clipBuffer.Get(geom.clips, state.clip);

    let stride = geom.boneCount * cFloat4sPerBone;

    PoseInto(geom, clip, state, gUniforms.time, state.paletteBase, localIdx);
    PoseInto(geom, clip, state, gUniforms.prevTime, state.paletteBase + stride, localIdx);
}
