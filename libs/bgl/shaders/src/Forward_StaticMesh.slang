import types.SubmeshInstance;
import forward.ForwardData;
import idl.Constants;
import idl.Mesh;
import idl.Meshlet;
import idl.PsoType;
import idl.Submesh;
import idl.VertexLayout;
import types.ByteBuffer;
import forward.common;
import forward.vertexdecode;
import debug.dbg;
import idl.ErrorCode;

// forwardData uniform
import uniforms.ForwardData;

// Returns its result rather than writing through the mesh output. Slang's Metal backend crashes on
// any function that takes an OutputVertices/OutputIndices parameter, so only MSMain may index them.
ForwardVSOut EvaluateVertex(Mesh mesh, Submesh submesh, SubmeshInstance instance, Meshlet meshlet, uint v)
{
    uint vertexIdx = forwardData.vertexMapBuffer[submesh.vertexMap, meshlet.relativeVertexOffset + v];

#if defined(BGL_DBG_VERTEX_SAFE)
    dbg_assert(vertexIdx < submesh.vertexCount, ErrorCode.kInvalidVertexIndex, vertexIdx, submesh.vertexCount, instance.submeshIndex);
#endif

    uint vertexByteBase = submesh.vertexData.GetStart() * 4 + vertexIdx * submesh.layout.stride;
    DecodedVertex vertex = DecodeVertex(forwardData.vertexDataBuffer, submesh.layout, vertexByteBase);

    float4 worldPos = mul(mesh.transform, float4(vertex.position, 1.0));

    // The mesh transform is fixed for an instance's lifetime, so the same world position reprojects
    // through both cameras; a movable instance would carry its own previous transform here.
    ForwardVSOut vertexOut;
    vertexOut.position = mul(forwardData.viewProj, worldPos);
    vertexOut.clip = vertexOut.position;
    vertexOut.prevClip = mul(forwardData.prevViewProj, worldPos);
    vertexOut.worldPos = worldPos.xyz;
    vertexOut.normal = mul((float3x3)mesh.transform, vertex.normal);
    vertexOut.uv = vertex.uv;
    vertexOut.tangent = float4(mul((float3x3)mesh.transform, vertex.tangent.xyz), vertex.tangent.w);

    vertexOut.materialIndex = instance.material.GetOffset();
    vertexOut.materialIsLoose = IsLoosePso(PsoType(instance.pso)) ? 1u : 0u;

    return vertexOut;
}

uint3 EvaluatePrimitive(Submesh submesh, Meshlet meshlet, uint t)
{
    return forwardData.indexBuffer.Get<3>(submesh.indices, meshlet.relativeIndexOffset + t * 3);
}

#if defined(BGL_WGSL)

// WebGPU has no mesh stage, so the meshlet is walked one output vertex at a time: the expansion pass
// wrote one record per (instance, meshlet), and the draw covers each record with a fixed
// cMaxPrimsPerMeshlet * 3 vertices. Everything below the index lookup is the shared code MSMain
// runs -- only the addressing differs.
[shader("vertex")]
ForwardVSOut VSMain(uint vertexId: SV_VertexID)
{
    const uint cVerticesPerRecord = cMaxPrimsPerMeshlet * 3;

    uint record = vertexId / cVerticesPerRecord;
    uint corner = vertexId % 3;
    uint prim = (vertexId % cVerticesPerRecord) / 3;

    let meshletInstance = forwardData.meshletInstances[record];
    let instance = forwardData.instanceBuffer.Get(meshletInstance.instanceIndex);
    let mesh = forwardData.meshBuffer.Get(instance.meshInstance);
    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);
    let meshlet = forwardData.meshletBuffer.Get(submesh.meshlets, meshletInstance.meshletIndex);

    // The draw is sized for a full meshlet, so the tail of a partial one has no triangle to emit.
    // Collapsing the primitive to a point leaves it zero-area rather than relying on a NaN position,
    // which WGSL will not const-fold.
    if (prim >= meshlet.triangleCount)
    {
        ForwardVSOut degenerate;
        degenerate.position = float4(0.0, 0.0, 0.0, 1.0);
        degenerate.clip = degenerate.position;
        degenerate.prevClip = degenerate.position;
        degenerate.worldPos = float3(0.0, 0.0, 0.0);
        degenerate.normal = float3(0.0, 0.0, 1.0);
        degenerate.uv = float2(0.0, 0.0);
        degenerate.tangent = float4(0.0, 0.0, 0.0, 0.0);
        degenerate.materialIndex = 0u;
        degenerate.materialIsLoose = 0u;
        return degenerate;
    }

    uint localVertex = forwardData.indexBuffer.Get(submesh.indices, meshlet.relativeIndexOffset + prim * 3 + corner);

    return EvaluateVertex(mesh, submesh, instance, meshlet, localVertex);
}

#else

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(
    uint gid: SV_GroupID,       // Total Instances
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    ASBase(forwardData, gid, gtid);
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

#endif
