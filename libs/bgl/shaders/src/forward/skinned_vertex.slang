// The skinned tier's vertex evaluation: the bind-pose vertex bytes blended by a pose.
//
// Where the pose comes from is the instance's, not the geom's, and it is the kind of playback record
// the placement holds: a SkinnedState owns a palette slice SkinnedPosePass wrote for it, a
// SkinnedTableState owns nothing and reads its rig's shared table.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: Forward_SkinnedMesh, which draws the PSO-bucketed phase, and
// Forward_AnyMesh, which draws lists that mix tiers.

import clip_playback;
import idl.Clip;
import types.SubmeshInstance;
import forward.SkinnedData;
import forward.common;
import forward.mesh_stage;
import forward.vertexdecode;
import idl.Constants;
import idl.PlaybackType;
import idl.Rig;
import idl.SkinnedState;
import idl.SkinnedTableState;
import debug.dbg;

public ConstantBuffer<SkinnedData> skinnedData;

// One bone's skinning matrix, unpacked from the three rows the pose pass stored. The fourth row of an
// affine transform is always (0,0,0,1), which is why it was not written.
float4x4 BoneMatrix(uint paletteBase, uint bone)
{
    let row = paletteBase + bone * cFloat4sPerBone;
    return float4x4(
        skinnedData.bonePaletteBuffer[row + 0],
        skinnedData.bonePaletteBuffer[row + 1],
        skinnedData.bonePaletteBuffer[row + 2],
        float4(0.0, 0.0, 0.0, 1.0)
    );
}

// Linear blend skinning: the weighted sum of the influencing bones' matrices, applied once rather
// than per attribute, so position, normal and tangent all follow the same blend.
//
// No renormalising divide: the importer already normalises a vertex whose weights sum to anything
// other than zero (assetlib's readSkinAttributes), so paying for it per vertex here would only paper
// over a bad cook. A vertex bound to no bone never reaches here -- SkinnedVertex answers that one
// for both pose sources.
float4x4 SkinMatrix(uint paletteBase, float4 joints, float4 weights)
{
    float4x4 skin = weights.x * BoneMatrix(paletteBase, uint(joints.x));

    [unroll]
    for (uint i = 1; i < cInfluencesPerVertex; ++i)
    {
        // A zero weight still costs the fetch; branching on it diverges the wave for no saving, since
        // the influences of one vertex are almost always all live or all dead together.
        skin += weights[i] * BoneMatrix(paletteBase, uint(joints[i]));
    }

    return skin;
}

// One bone's matrix out of the rig's table, at an absolute frame of the rig's clip set. The table is
// frame-major, addressed exactly as the sample pool it was posed from.
float4x4 TableMatrix(Rig rig, uint frame, uint bone)
{
    let slot = rig.boneAnimTable.GetStart() + (frame * rig.boneCount + bone) * cFloat4sPerBone;
    return float4x4(
        skinnedData.boneAnimTables[slot + 0],
        skinnedData.boneAnimTables[slot + 1],
        skinnedData.boneAnimTables[slot + 2],
        float4(0.0, 0.0, 0.0, 1.0)
    );
}

// The palette read's twin, against the table at one frame: same blend, different fetch.
float4x4 TableSkinMatrix(Rig rig, uint frame, float4 joints, float4 weights)
{
    float4x4 skin = weights.x * TableMatrix(rig, frame, uint(joints.x));

    [unroll]
    for (uint i = 1; i < cInfluencesPerVertex; ++i)
    {
        skin += weights[i] * TableMatrix(rig, frame, uint(joints[i]));
    }

    return skin;
}

// A fractional frame is the blend of the two frames' *skin matrices*, not of two skinned positions:
// skinning is linear in the matrices, so the two agree, and this pays one blend per vertex instead
// of one per attribute. The frame indices are clip-local and the table is addressed globally, so the
// clip's own base is added to each.
float4x4 SkinFromTable(Rig rig, Clip clip, float frames, float4 joints, float4 weights)
{
    uint f0;
    uint f1;
    float weight;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    return lerp(
        TableSkinMatrix(rig, clip.firstFrame + f0, joints, weights), TableSkinMatrix(rig, clip.firstFrame + f1, joints, weights), weight
    );
}

// Called from each branch below rather than once above them, because each reads its rig out of a
// record of its own kind. Gated, not just dbg_assert'd: only dbg_raise's *body* is compiled out, so
// the reduction would otherwise cost three max()s per vertex in a release build.
void AssertBoneIndices(Rig rig, float4 joints, uint submeshIndex)
{
#if defined(BERNINI_GPU_DEBUG)
    dbg_assert(
        uint(max(max(joints.x, joints.y), max(joints.z, joints.w))) < rig.boneCount,
        ErrorCode.kInvalidBoneIndex,
        uint(joints.x),
        rig.boneCount,
        submeshIndex
    );
#endif
}

public ForwardVSOut SkinnedVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    // Every branch produces this frame's matrix and the one the previous frame drew, because the
    // motion vector needs both -- and none of them remembers anything, so an instance spawned
    // mid-frame is correct on its first. That holds only while time is the sole input to a pose: a
    // clip switched between frames would reproject through the wrong clip.
    float4x4 skin;
    float4x4 prevSkin;

    // Zero weights are ordinary rather than broken input: an exporter writes four of them for a
    // vertex it never assigned to a bone, and summing those gives a zero matrix that would collapse
    // the vertex onto the origin. The bind pose is what assetlib::skinSubmesh -- the CPU reference
    // both sources are measured against -- returns for it. Answered ahead of the pose source because
    // neither can answer it differently, and it needs no record to answer.
    if (dot(vertex.weights, float4(1.0, 1.0, 1.0, 1.0)) <= 0.0)
    {
        skin = float4x4(float4(1.0, 0.0, 0.0, 0.0), float4(0.0, 1.0, 0.0, 0.0), float4(0.0, 0.0, 1.0, 0.0), float4(0.0, 0.0, 0.0, 1.0));
        prevSkin = skin;
    }
    // The record's kind is the pose source. Read from the arena header rather than from a field,
    // because the two kinds are different records and not one record with a hole in it.
    else if (forwardData.GetPlaybackKind(ctx.mesh.playback) == PlaybackType.kSkinnedTable)
    {
        let state = forwardData.playbackBuffer.LoadRecordAs<SkinnedTableState>(ctx.mesh.playback);
        let rig = skinnedData.rigBuffer.Get(state.rig);
        AssertBoneIndices(rig, vertex.joints, instance.submeshIndex);

        Clip clip = skinnedData.clipBuffer.Get(rig.clips, state.clip);

        skin = SkinFromTable(rig, clip, ClipFrames(clip, state.phase, state.rate, viewData.time), vertex.joints, vertex.weights);
        prevSkin = SkinFromTable(rig, clip, ClipFrames(clip, state.phase, state.rate, viewData.prevTime), vertex.joints, vertex.weights);
    }
    else
    {
        let state = forwardData.playbackBuffer.LoadRecordAs<SkinnedState>(ctx.mesh.playback);
        let rig = skinnedData.rigBuffer.Get(state.rig);
        AssertBoneIndices(rig, vertex.joints, instance.submeshIndex);

        // The pose pass wrote the previous frame's into the second half of this instance's slice, so
        // the reprojection is one more blend rather than a history buffer.
        let base = state.palette.GetStart();
        skin = SkinMatrix(base, vertex.joints, vertex.weights);
        prevSkin = SkinMatrix(base + rig.boneCount * cFloat4sPerBone, vertex.joints, vertex.weights);
    }

    float4 worldPos = mul(ctx.mesh.transform, mul(skin, float4(vertex.position, 1.0)));
    float4 prevWorldPos = mul(ctx.mesh.transform, mul(prevSkin, float4(vertex.position, 1.0)));

    ForwardVSOut vertexOut;
    ProjectVertex(vertexOut, worldPos, prevWorldPos);

    // The skin matrix carries the bones' rotation, so the basis follows it before the instance
    // transform does. Not renormalized, exactly as the static tier leaves it.
    float3x3 skinRotation = (float3x3)skin;
    vertexOut.normal = mul((float3x3)ctx.mesh.transform, mul(skinRotation, vertex.normal));

    vertexOut.uv = vertex.uv;

    // The tangent is skinned by the same basis as the normal. w is handedness and must survive the
    // transform untouched.
    vertexOut.tangent = float4(mul((float3x3)ctx.mesh.transform, mul(skinRotation, vertex.tangent.xyz)), vertex.tangent.w);

    vertexOut.materialOffset = instance.material.GetByteOffset();

    return vertexOut;
}
