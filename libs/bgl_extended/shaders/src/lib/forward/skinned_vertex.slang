// The skinned tier's vertex evaluation: the bind-pose vertex bytes blended by a pose.
//
// Where the pose comes from is the instance's, not the geom's, and it is the kind of playback record
// the placement holds: a SkinnedState owns a palette slice SkinnedPosePass wrote for it, a
// SkinnedTableState owns nothing and reads its rig's shared table. That question is answered once
// per mesh-shader group, in ResolveSkinnedPose, because one group draws one instance -- so the
// blend below never learns which source it is reading.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: programs/forward/SkinnedMesh, which draws the PSO-bucketed phase,
// and programs/forward/AnyMesh, which draws lists that mix tiers.

import idl.Clip;
import idl.Constants;
import idl.IBonePalette;
import idl.PlaybackType;
import idl.Rig;
import idl.SkinnedState;
import idl.SkinnedTableState;
import lib.anim.clip_playback;
import lib.anim.skinning;
import lib.debug.dbg;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.SkinnedData;
import lib.forward.vertexdecode;
import lib.types.ComputeBuffer;
import lib.data.SubmeshInstance;

public ConstantBuffer<SkinnedData> skinnedData;

// One mesh-shader group's pose, resolved for the group rather than for each of its vertices: which
// buffer holds the pose, where in it this instance's bone 0 sits, and which frames the blend spans.
// A vertex contributes only its joints and weights.
public struct SkinnedPose : IBonePalette
{
    // The palette SkinnedPosePass wrote for this instance, or the table its rig shares. Which of the
    // two is the whole of the difference between the pose sources, and it is settled here.
    ComputeBuffer<float4> source;

    float4 GetPalette(uint slot) { return source[slot]; }

    // Bone 0 of this frame's pose, and of the pose the previous frame drew.
    uint base;
    uint prevBase;

    // The second frame each pose falls between, and its weight. Only the shared table stores whole
    // frames; a palette already holds the interpolated pose, so `blends` is false on that source and
    // these are never read.
    uint upperBase;
    uint prevUpperBase;
    float weight;
    float prevWeight;
    bool blends;

    // Bounds the bone-index assertion, which is the only thing here that needs the rig.
    uint boneCount;
};

/**
 * Resolves the pose one group draws from. Every load here is group-uniform, which is why it happens
 * once instead of per vertex.
 *
 * @pre ctx.mesh.playback is non-null -- a static placement owns no record, and the entry point that
 *      draws mixed tiers branches on that before reaching here.
 */
public SkinnedPose ResolveSkinnedPose(MeshletContext ctx)
{
    SkinnedPose pose;

    // The record's kind is the pose source. Read from the arena header rather than from a field,
    // because the two kinds are different records and not one record with a hole in it.
    if (forwardData.GetPlaybackKind(ctx.mesh.playback) == PlaybackType.kSkinnedTable)
    {
        let state = forwardData.playbackBuffer.LoadRecordAs<SkinnedTableState>(ctx.mesh.playback);
        let rig = skinnedData.rigBuffer.Get(state.playback.rig);
        let clip = skinnedData.clipBuffer.Get(rig.clips, state.playback.clip);

        pose.source = skinnedData.boneAnimTables;
        pose.blends = true;
        pose.boneCount = rig.boneCount;

        TableFrameSlots(rig, clip, ClipFrames(clip, state.playback, viewData.time), pose.base, pose.upperBase, pose.weight);
        TableFrameSlots(rig, clip, ClipFrames(clip, state.playback, viewData.prevTime), pose.prevBase, pose.prevUpperBase, pose.prevWeight);

        return pose;
    }

    let state = forwardData.playbackBuffer.LoadRecordAs<SkinnedState>(ctx.mesh.playback);
    let rig = skinnedData.rigBuffer.Get(state.playback.rig);

    pose.source = skinnedData.bonePaletteBuffer;
    pose.blends = false;
    pose.boneCount = rig.boneCount;

    // The pose pass wrote the previous frame's into the second half of this instance's slice, so the
    // reprojection is one more blend rather than a history buffer.
    pose.base = state.palette.GetStart();
    pose.prevBase = pose.base + rig.boneCount * cFloat4sPerBone;

    pose.upperBase = pose.base;
    pose.prevUpperBase = pose.prevBase;
    pose.weight = 0.0;
    pose.prevWeight = 0.0;

    return pose;
}

// Gated, not just dbg_assert'd: only dbg_raise's *body* is compiled out, so the reduction would
// otherwise cost three max()s per vertex in a release build.
void AssertBoneIndices(SkinnedPose pose, float4 joints, uint submeshIndex)
{
#if defined(BERNINI_GPU_DEBUG)
    dbg_assert(
        uint(max(max(joints.x, joints.y), max(joints.z, joints.w))) < pose.boneCount,
        ErrorCode.kInvalidBoneIndex,
        uint(joints.x),
        pose.boneCount,
        submeshIndex
    );
#endif
}

public ForwardVSOut SkinnedVertex(MeshletContext ctx, SkinnedPose pose, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    // Both this frame's matrix and the one the previous frame drew, because the motion vector needs
    // both -- and neither remembers anything, so an instance spawned mid-frame is correct on its
    // first. That holds only while time is the sole input to a pose: a clip switched between frames
    // would reproject through the wrong clip.
    float4x4 skin;
    float4x4 prevSkin;

    // Zero weights are ordinary rather than broken input: an exporter writes four of them for a
    // vertex it never assigned to a bone, and summing those gives a zero matrix that would collapse
    // the vertex onto the origin. The bind pose is what assetlib::skinSubmesh -- the CPU reference
    // both sources are measured against -- returns for it. Answered ahead of the blend because
    // neither source can answer it differently, and its joints name no bone to assert against.
    if (dot(vertex.weights, float4(1.0, 1.0, 1.0, 1.0)) <= 0.0)
    {
        skin = float4x4(float4(1.0, 0.0, 0.0, 0.0), float4(0.0, 1.0, 0.0, 0.0), float4(0.0, 0.0, 1.0, 0.0), float4(0.0, 0.0, 0.0, 1.0));
        prevSkin = skin;
    }
    else
    {
        AssertBoneIndices(pose, vertex.joints, instance.submeshIndex);

        skin = SkinAt(pose, pose.blends, pose.base, pose.upperBase, pose.weight, vertex.joints, vertex.weights);
        prevSkin = SkinAt(pose, pose.blends, pose.prevBase, pose.prevUpperBase, pose.prevWeight, vertex.joints, vertex.weights);
    }

    float4 worldPos = float4(ctx.mesh.TransformPoint(mul(skin, float4(vertex.position, 1.0)).xyz), 1.0);
    float4 prevWorldPos = float4(ctx.mesh.TransformPoint(mul(prevSkin, float4(vertex.position, 1.0)).xyz), 1.0);

    ForwardVSOut vertexOut;
    ProjectVertex(vertexOut, worldPos, prevWorldPos);

    // The skin matrix carries the bones' rotation, so the basis follows it before the instance
    // transform does. Not renormalized, exactly as the static tier leaves it.
    float3x3 skinRotation = (float3x3)skin;
    vertexOut.normal = ctx.mesh.TransformDirection(mul(skinRotation, vertex.normal));

    vertexOut.uv = vertex.uv;

    // The tangent is skinned by the same basis as the normal. w is handedness and must survive the
    // transform untouched.
    vertexOut.tangent = float4(ctx.mesh.TransformDirection(mul(skinRotation, vertex.tangent.xyz)), vertex.tangent.w);

    vertexOut.materialOffset = instance.material.GetByteOffset();

    return vertexOut;
}
