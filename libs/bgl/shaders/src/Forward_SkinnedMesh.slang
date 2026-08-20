import types.SubmeshInstance;
import forward.ForwardData;
import forward.SkinnedData;
import forward.ViewData;
import idl.Constants;
import idl.Mesh;
import idl.Meshlet;
import idl.PsoType;
import idl.Submesh;
import idl.SkinnedGeom;
import idl.SkinnedState;
import forward.common;
import forward.vertexdecode;
import debug.dbg;
import forward.ExpansionData;

// The skinned geometry stage: position, normal and tangent are the bind-pose vertex bytes blended by
// the instance's bone palette, which SkinnedPosePass wrote for this frame. Everything downstream of
// ForwardVSOut is the static path's, untouched.

ConstantBuffer<ForwardData> forwardData;
ConstantBuffer<ViewData> viewData;
ConstantBuffer<SkinnedData> skinnedData;

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
float4x4 SkinMatrix(SkinnedGeom geom, uint paletteBase, float4 joints, float4 weights)
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

// Returns its result rather than writing through the mesh output. Slang's Metal backend crashes on
// any function that takes an OutputVertices/OutputIndices parameter, so only MSMain may index them.
ForwardVSOut EvaluateVertex(Mesh mesh, Submesh submesh, SubmeshInstance instance, Meshlet meshlet, uint v)
{
    uint vertexIdx = forwardData.vertexMapBuffer[submesh.vertexMap, meshlet.relativeVertexOffset + v];

    dbg_assert(vertexIdx < submesh.vertexCount, ErrorCode.kInvalidVertexIndex, vertexIdx, submesh.vertexCount, instance.submeshIndex);

    uint vertexByteBase = submesh.vertexData.GetStart() * 4 + vertexIdx * submesh.layout.stride;
    DecodedVertex vertex = DecodeVertex(forwardData.vertexDataBuffer, submesh.layout, vertexByteBase);

    let state = skinnedData.skinnedStateBuffer.Get(mesh.skinnedState);
    let geom = skinnedData.skinnedGeomBuffer.Get(state.geom);

#if defined(BERNINI_GPU_DEBUG)
    // Gated, not just dbg_assert'd: only dbg_raise's *body* is compiled out, so the reduction below
    // would otherwise cost three max()s per vertex in a release build.
    dbg_assert(
        uint(max(max(vertex.joints.x, vertex.joints.y), max(vertex.joints.z, vertex.joints.w))) < geom.boneCount,
        ErrorCode.kInvalidBoneIndex,
        uint(vertex.joints.x),
        geom.boneCount,
        instance.submeshIndex
    );
#endif

    let base = state.palette.GetStart();
    float4x4 skin = SkinMatrix(geom, base, vertex.joints, vertex.weights);

    float4 skinnedPos = mul(skin, float4(vertex.position, 1.0));
    float4 worldPos = mul(mesh.transform, skinnedPos);

    ForwardVSOut vertexOut;
    vertexOut.position = mul(viewData.viewProj, worldPos);

    // Both matrices carry their frame's sub-pixel offset; the raster keeps it and the motion vector
    // must not, so it comes back out here rather than in the pixel stage.
    vertexOut.clip = vertexOut.position;
    vertexOut.clip.xy -= viewData.jitter * vertexOut.position.w;

    // The pose the previous frame drew, through the previous frame's camera. The pose pass wrote it
    // into the second half of this instance's slice, so the reprojection is one more blend rather
    // than a history buffer. That holds only while time is the sole input to a pose: a clip switched
    // between frames would reproject through the wrong clip.
    let prevBase = base + geom.boneCount * cFloat4sPerBone;
    float4x4 prevSkin = SkinMatrix(geom, prevBase, vertex.joints, vertex.weights);
    float4 prevWorldPos = mul(mesh.transform, mul(prevSkin, float4(vertex.position, 1.0)));

    vertexOut.prevClip = mul(viewData.prevViewProj, prevWorldPos);
    vertexOut.prevClip.xy -= viewData.prevJitter * vertexOut.prevClip.w;
    vertexOut.worldPos = worldPos.xyz;

    // The skin matrix carries the bones' rotation, so the basis follows it before the instance
    // transform does. Not renormalized, exactly as the static path leaves it.
    float3x3 skinRotation = (float3x3)skin;
    vertexOut.normal = mul((float3x3)mesh.transform, mul(skinRotation, vertex.normal));

    vertexOut.uv = vertex.uv;

    // The tangent is skinned by the same basis as the normal. w is handedness and must survive the
    // transform untouched.
    vertexOut.tangent = float4(mul((float3x3)mesh.transform, mul(skinRotation, vertex.tangent.xyz)), vertex.tangent.w);

    vertexOut.materialIndex = instance.material.GetOffset();
    vertexOut.materialIsLoose = 0u;

    return vertexOut;
}

uint3 EvaluatePrimitive(Submesh submesh, Meshlet meshlet, uint t)
{
    return forwardData.indexBuffer.Get<3>(submesh.indices, meshlet.relativeIndexOffset + t * 3);
}

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(uint gid: SV_GroupID)
{
    uint drawInstanceIdx = expansionData.compactedInstances[ResolveInstanceBase() + gid];
    let instance = forwardData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = forwardData.meshBuffer.Get(instance.meshInstance);

    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload InstancedPayload payload,
    OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    let mesh = forwardData.meshBuffer.Get(payload.instance.meshInstance);

    dbg_assert(
        payload.instance.submeshIndex < mesh.submeshes.count,
        ErrorCode.kInvalidSubmeshIndex,
        payload.instance.submeshIndex,
        mesh.submeshes.count,
        payload.instance.meshInstance.GetOffset()
    );

    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, payload.instance.submeshIndex);

    dbg_assert(
        gid < submesh.GetMeshletCount(), ErrorCode.kInvalidMeshletIndex, gid, submesh.GetMeshletCount(), payload.instance.submeshIndex
    );
    dbg_assert(submesh.layout.stride > 0, ErrorCode.kInvalidVertexLayout, submesh.layout.stride, 0, payload.instance.submeshIndex);

    let meshlet = forwardData.meshletBuffer.Get(submesh.meshlets, gid);

    dbg_assert(
        meshlet.vertexCount <= cMaxVerticesPerMeshlet, ErrorCode.kMeshletVertexOverflow, meshlet.vertexCount, cMaxVerticesPerMeshlet, gid
    );
    dbg_assert(
        meshlet.triangleCount <= cMaxPrimsPerMeshlet, ErrorCode.kMeshletPrimitiveOverflow, meshlet.triangleCount, cMaxPrimsPerMeshlet, gid
    );

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    for (uint v = gtid; v < meshlet.vertexCount; v += cMeshGroupSize)
    {
        verts[v] = EvaluateVertex(mesh, submesh, payload.instance, meshlet, v);
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    for (uint t = gtid; t < meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = EvaluatePrimitive(submesh, meshlet, t);
    }
}
