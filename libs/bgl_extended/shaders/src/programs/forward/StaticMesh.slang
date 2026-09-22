import idl.Constants;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.static_vertex;

// The static forward geometry stage. The shared expansion, one tier's vertex evaluation.

[numthreads(cMeshletCullGroupSize, 1, 1)]
[shader("amplification")]
void ASMain(uint gid: SV_GroupID, uint gtid: SV_GroupThreadID)
{
    let meshletCount = CullMeshlets(gid, gtid);

    DispatchMesh(meshletCount, 1, 1, gCulledPayload);
}

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload CulledPayload payload,
    OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    // The amplification stage keeps whole groups, so this group's meshlet is one the group's bound
    // reached the frustum for rather than one this meshlet did -- and the last group of a submesh
    // whose meshlet count does not divide stands for meshlets that do not exist.
    let meshletIdx = CulledMeshletIndex(payload, gid);

    uint vertexCount = 0u;
    uint triangleCount = 0u;
    MeshletContext ctx;
    bool visible = meshletIdx < payload.meshletCount;

    if (visible)
    {
        ctx = PrepareMeshlet(payload.instance, meshletIdx);
        visible = LocalSphereInCullFrustum(ctx.mesh, ctx.meshlet.boundingSphere);
        vertexCount = visible ? ctx.meshlet.vertexCount : 0u;
        triangleCount = visible ? ctx.meshlet.triangleCount : 0u;
    }

    // One call site for the counts, whichever way that goes: DXC refuses a second.
    SetMeshOutputCounts(vertexCount, triangleCount);
    if (!visible)
    {
        return;
    }

    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    for (uint v = gtid; v < ctx.meshlet.vertexCount; v += cMeshGroupSize)
    {
        let vertex = StaticVertex(ctx, payload.instance, v);
        verts[v] = vertex;
        if (ctx.cull)
        {
            RecordClip(v, vertex.position);
        }
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    // Every vertex's clip position is on record before any triangle judges its facing. Gated on the
    // draw, not the material: uniform across the group either way, but only the cbuffer value is
    // uniform to the compiler.
    if (DrawCullsBackfaces())
    {
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint t = gtid; t < ctx.meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = CullBackface(ctx, EvaluatePrimitive(ctx, t));
    }
}
