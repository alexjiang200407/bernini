import idl.Constants;
import idl.PlaybackType;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.skinned_vertex;
import lib.forward.static_vertex;
import lib.forward.vat_vertex;
import lib.types.SubmeshInstance;

// The tier-branching forward geometry stage, for a draw whose instance list mixes tiers: the
// depth-sorted transparent list, whose order is depth and not PSO, and the outline mask's selection.
//
// Which tier an instance belongs to is read off its own playback record's header, so nothing here
// mirrors the PSO table. The branch is uniform across a mesh-shader group, one instance being one
// group's worth of work, so it costs register pressure and not divergence.
ForwardVSOut EvaluateVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    // A static placement owns no record, so there is no header to read -- null is the tier.
    if (ctx.mesh.playback.Null())
    {
        return StaticVertex(ctx, instance, v);
    }

    if (forwardData.GetPlaybackKind(ctx.mesh.playback) == PlaybackType.kVat)
    {
        return VatVertex(ctx, instance, v);
    }

    return SkinnedVertex(ctx, instance, v);
}

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
    for (uint v = gtid; v < ctx.meshlet.vertexCount; v += cMeshGroupSize)
    {
        verts[v] = EvaluateVertex(ctx, payload.instance, v);
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    for (uint t = gtid; t < ctx.meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = EvaluatePrimitive(ctx, t);
    }
}
