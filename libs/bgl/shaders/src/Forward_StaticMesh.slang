import types.SubmeshInstance;
import forward.ForwardData;
import idl.Constants;
import idl.Mesh;
import idl.Meshlet;
import idl.Submesh;
import idl.VertexLayout;
import types.ByteBuffer;
import forward.common;
import forward.vertexdecode;
import debug.dbg;
import idl.ErrorCode;

// forwardData uniform
import uniforms.ForwardData;

void EvaluateVertices(
    Mesh mesh,
    Submesh submesh,
    Meshlet meshlet,
    uint gtid,
    inout OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts
)
{
    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    for (uint v = gtid; v < meshlet.vertexCount; v += cMeshGroupSize)
    {
        uint vertexIdx = forwardData.vertexMapBuffer[submesh.vertexMap, meshlet.relativeVertexOffset + v];

        dbg_assert(vertexIdx < submesh.vertexCount, ErrorCode.kInvalidVertexIndex);

        uint vertexByteBase = submesh.vertexData.GetStart() * 4 + vertexIdx * submesh.layout.stride;
        DecodedVertex vertex = DecodeVertex(forwardData.vertexDataBuffer, submesh.layout, vertexByteBase);

        float4 worldPos = mul(mesh.transform, float4(vertex.position, 1.0));

        ForwardVSOut vertexOut;
        vertexOut.position = mul(forwardData.viewProj, worldPos);
        vertexOut.worldPos = worldPos.xyz;
        vertexOut.normal = mul((float3x3)mesh.transform, vertex.normal);
        vertexOut.uv = vertex.uv;
        vertexOut.tangent = float4(mul((float3x3)mesh.transform, vertex.tangent.xyz), vertex.tangent.w);
        vertexOut.materialIndex = submesh.material.GetOffset();

        verts[v] = vertexOut;
    }
}

void EvaluatePrimitives(Submesh submesh, Meshlet meshlet, uint gtid, inout OutputIndices<uint3, cMaxPrimsPerMeshlet> tris)
{
    // Same striding as the vertices: cMaxPrimsPerMeshlet (124) triangles do not fit one per thread.
    // Every triangle SetMeshOutputCounts declared must be written, or the unwritten ones rasterize
    // from undefined indices.
    for (uint t = gtid; t < meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = forwardData.indexBuffer.Get<3>(submesh.indices, meshlet.relativeIndexOffset + t * 3);
    }
}

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

    dbg_assert(payload.instance.submeshIndex < mesh.submeshes.count, ErrorCode.kInvalidSubmeshIndex);

    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, payload.instance.submeshIndex);

    dbg_assert(gid < submesh.GetMeshletCount(), ErrorCode.kInvalidMeshletIndex);
    dbg_assert(submesh.layout.stride > 0, ErrorCode.kInvalidVertexLayout);

    let meshlet = forwardData.meshletBuffer.Get(submesh.meshlets, gid);

    dbg_assert(meshlet.vertexCount <= cMaxVerticesPerMeshlet, ErrorCode.kMeshletVertexOverflow);
    dbg_assert(meshlet.triangleCount <= cMaxPrimsPerMeshlet, ErrorCode.kMeshletPrimitiveOverflow);

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    EvaluateVertices(mesh, submesh, meshlet, gtid, verts);
    EvaluatePrimitives(submesh, meshlet, gtid, tris);
}
