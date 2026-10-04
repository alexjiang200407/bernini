import idl.Constants;
import lib.forward.common;
import lib.forward.mesh_stage;
import idl.ToonShadingRigPool;
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
        ctx = PrepareMeshlet(payload.instance, payload.submeshEntry, payload.lodDissolve, meshletIdx);
        visible = LocalSphereInCullFrustum(ctx.mesh, ctx.meshlet.boundingSphere);
        vertexCount = visible ? ctx.meshlet.vertexCount : 0u;
        triangleCount = visible ? ctx.meshlet.triangleCount : 0u;
    }

    // One call site for the counts, whichever way that goes: DXC refuses a second.
    SetMeshOutputCounts(vertexCount, triangleCount);

    // No early return for a culled meshlet: every thread must reach the barrier below. The counts
    // are zero for one, so both loops are empty.
    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    for (uint v = gtid; v < vertexCount; v += cMeshGroupSize)
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

    for (uint t = gtid; t < triangleCount; t += cMeshGroupSize)
    {
        tris[t] = CullBackface(ctx, EvaluatePrimitive(ctx, t));
    }
}

// MSMain for the bucket's dissolve lane: every vertex also carries its placement's dissolve code,
// which MSMain leaves out so a placement at rest pays no interpolant for it. A copy rather than one
// generic body because Slang cannot hand mesh outputs to a function on Metal.
[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSDissolve(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload CulledPayload payload,
    OutputVertices<DissolveVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    let meshletIdx = CulledMeshletIndex(payload, gid);

    uint vertexCount = 0u;
    uint triangleCount = 0u;
    MeshletContext ctx;
    bool visible = meshletIdx < payload.meshletCount;

    if (visible)
    {
        ctx = PrepareMeshlet(payload.instance, payload.submeshEntry, payload.lodDissolve, meshletIdx);
        visible = LocalSphereInCullFrustum(ctx.mesh, ctx.meshlet.boundingSphere);
        vertexCount = visible ? ctx.meshlet.vertexCount : 0u;
        triangleCount = visible ? ctx.meshlet.triangleCount : 0u;
    }

    SetMeshOutputCounts(vertexCount, triangleCount);

    for (uint v = gtid; v < vertexCount; v += cMeshGroupSize)
    {
        let vertex = StaticVertex(ctx, payload.instance, v);
        verts[v] = DissolveVSOut(vertex, ctx.lodDissolve);
        if (ctx.cull)
        {
            RecordClip(v, vertex.position);
        }
    }

    if (DrawCullsBackfaces())
    {
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint t = gtid; t < triangleCount; t += cMeshGroupSize)
    {
        tris[t] = CullBackface(ctx, EvaluatePrimitive(ctx, t));
    }
}

// MSMain for a toon character bucket at rest: every vertex also carries its placement's evaluated
// toon shading rig block, which MSMain leaves out so no other draw pays an interpolant for it. A
// copy for the reason MSDissolve is one.
[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSToon(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload CulledPayload payload,
    OutputVertices<ToonVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    let meshletIdx = CulledMeshletIndex(payload, gid);

    uint vertexCount = 0u;
    uint triangleCount = 0u;
    uint rigSlot = 0u;
    MeshletContext ctx;
    bool visible = meshletIdx < payload.meshletCount;

    if (visible)
    {
        ctx = PrepareMeshlet(payload.instance, payload.submeshEntry, payload.lodDissolve, meshletIdx);
        visible = LocalSphereInCullFrustum(ctx.mesh, ctx.meshlet.boundingSphere);
        vertexCount = visible ? ctx.meshlet.vertexCount : 0u;
        triangleCount = visible ? ctx.meshlet.triangleCount : 0u;
        rigSlot = (ctx.mesh.flags >> cToonShadingRigSlotShift) & cToonShadingRigSlotMask;
    }

    SetMeshOutputCounts(vertexCount, triangleCount);

    for (uint v = gtid; v < vertexCount; v += cMeshGroupSize)
    {
        let vertex = StaticVertex(ctx, payload.instance, v);
        verts[v] = ToonVSOut(vertex, rigSlot);
        if (ctx.cull)
        {
            RecordClip(v, vertex.position);
        }
    }

    if (DrawCullsBackfaces())
    {
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint t = gtid; t < triangleCount; t += cMeshGroupSize)
    {
        tris[t] = CullBackface(ctx, EvaluatePrimitive(ctx, t));
    }
}
