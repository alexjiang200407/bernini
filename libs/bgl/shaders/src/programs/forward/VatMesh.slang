import idl.Constants;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.vat_vertex;

// The VAT forward geometry stage. The shared expansion, one tier's vertex evaluation.

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
        verts[v] = VatVertex(ctx, payload.instance, v);
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    for (uint t = gtid; t < ctx.meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = EvaluatePrimitive(ctx, t);
    }
}
