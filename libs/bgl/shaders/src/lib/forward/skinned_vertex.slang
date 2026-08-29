// The skinned tier's vertex evaluation: the bind-pose vertex bytes blended by the instance's bone
// palette, which SkinnedPosePass wrote this frame.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: programs/forward/SkinnedMesh, which draws the PSO-bucketed phase,
// and programs/forward/AnyMesh, which draws lists that mix tiers.

import idl.Constants;
import idl.Rig;
import idl.SkinnedState;
import lib.debug.dbg;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.SkinnedData;
import lib.forward.vertexdecode;
import lib.types.SubmeshInstance;

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
// over a bad cook.
//
// Zero is the exception, and it is ordinary rather than broken input: an exporter writes four zero
// weights for a vertex it never assigned to a bone. Summing those gives a zero matrix, which would
// collapse the vertex onto the origin, so it falls back to identity -- the bind-pose vertex, which is
// exactly what assetlib::skinSubmesh (the CPU reference this path is measured against) returns for it.
float4x4 SkinMatrix(uint paletteBase, float4 joints, float4 weights)
{
    if (dot(weights, float4(1.0, 1.0, 1.0, 1.0)) <= 0.0)
    {
        return float4x4(float4(1.0, 0.0, 0.0, 0.0), float4(0.0, 1.0, 0.0, 0.0), float4(0.0, 0.0, 1.0, 0.0), float4(0.0, 0.0, 0.0, 1.0));
    }

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

public ForwardVSOut SkinnedVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    let state = forwardData.playbackBuffer.LoadRecordAs<SkinnedState>(ctx.mesh.playback);
    let rig = skinnedData.rigBuffer.Get(state.rig);

#if defined(BERNINI_GPU_DEBUG)
    // Gated, not just dbg_assert'd: only dbg_raise's *body* is compiled out, so the reduction below
    // would otherwise cost three max()s per vertex in a release build.
    dbg_assert(
        uint(max(max(vertex.joints.x, vertex.joints.y), max(vertex.joints.z, vertex.joints.w))) < rig.boneCount,
        ErrorCode.kInvalidBoneIndex,
        uint(vertex.joints.x),
        rig.boneCount,
        instance.submeshIndex
    );
#endif

    let base = state.palette.GetStart();
    float4x4 skin = SkinMatrix(base, vertex.joints, vertex.weights);

    float4 worldPos = mul(ctx.mesh.transform, mul(skin, float4(vertex.position, 1.0)));

    // The pose the previous frame drew. The pose pass wrote it into the second half of this
    // instance's slice, so the reprojection is one more blend rather than a history buffer. That
    // holds only while time is the sole input to a pose: a clip switched between frames would
    // reproject through the wrong clip.
    let prevBase = base + rig.boneCount * cFloat4sPerBone;
    float4x4 prevSkin = SkinMatrix(prevBase, vertex.joints, vertex.weights);
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
