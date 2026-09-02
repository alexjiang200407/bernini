import idl.Constants;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.skinned_vertex;
import lib.forward.static_vertex;
import lib.data.SubmeshInstance;

// The tier-branching forward geometry stage, for a draw whose instance list mixes tiers: the
// depth-sorted transparent list, whose order is depth and not PSO, and the outline mask's selection.
//
// Whether a placement is animated is whether it owns a playback record, so nothing here mirrors the
// PSO table; which of the two animated sources it draws from is ResolveSkinnedPose's own branch, on
// the record's header. Both are the group's rather than the vertex's, one instance being one group's
// worth of work, so both are settled outside the loop below.

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(uint gid: SV_GroupID)
{
    InstancedPayload payload;
    let meshletCount = ResolveDispatch(gid, payload);

    DispatchMesh(meshletCount, 1, 1, payload);
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
    let ctx = PrepareMeshlet(payload.instance, gid);

    SetMeshOutputCounts(ctx.meshlet.vertexCount, ctx.meshlet.triangleCount);

    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    //
    // The tier is the group's, so the loop is written twice rather than branched inside: a static
    // placement owns no record, and there is no header to read for one.
    if (ctx.mesh.playback.Null())
    {
        for (uint v = gtid; v < ctx.meshlet.vertexCount; v += cMeshGroupSize)
        {
            verts[v] = StaticVertex(ctx, payload.instance, v);
        }
    }
    else
    {
        let pose = ResolveSkinnedPose(ctx);

        for (uint v = gtid; v < ctx.meshlet.vertexCount; v += cMeshGroupSize)
        {
            verts[v] = SkinnedVertex(ctx, pose, payload.instance, v);
        }
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    for (uint t = gtid; t < ctx.meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = EvaluatePrimitive(ctx, t);
    }
}
