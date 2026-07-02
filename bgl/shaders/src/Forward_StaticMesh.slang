import types.SubmeshInstance;
import forward.ForwardData;
import util.constants;
import idl.Mesh;
import idl.Meshlet;
import idl.Submesh;
import idl.VertexLayout;
import types.ByteBuffer;
import forward.common;
import forward.vertexdecode;

ConstantBuffer<ForwardData> sceneData;

void EvaluateVertices(
    Mesh mesh,
    Submesh submesh,
    Meshlet meshlet,
    uint gtid,
    inout OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts
)
{
    if (gtid < meshlet.vertexCount)
    {
        uint vertexIdx = sceneData.vertexMapBuffer[submesh.vertexMap, meshlet.relativeVertexOffset + gtid];
        uint vertexByteBase = submesh.vertexData.GetStart() * 4 + vertexIdx * submesh.layout.stride;
        DecodedVertex v = DecodeVertex(sceneData.vertexDataBuffer, submesh.layout, vertexByteBase);

        float4 worldPos = mul(mesh.transform, float4(v.position, 1.0));

        ForwardVSOut vertexOut;
        vertexOut.position = mul(sceneData.viewProj, worldPos);
        vertexOut.normal = mul((float3x3)mesh.transform, v.normal);
        vertexOut.uv = v.uv;

        verts[gtid] = vertexOut;
    }
}

void EvaluatePrimitives(Submesh submesh, Meshlet meshlet, uint gtid, inout OutputIndices<uint3, cMaxPrimsPerMeshlet> tris)
{
    if (gtid < meshlet.triangleCount)
    {
        tris[gtid] = sceneData.indexBuffer.Get<3>(submesh.indices, meshlet.relativeIndexOffset + gtid * 3);
    }
}

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(
    uint gid: SV_GroupID,       // Total Instances
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    ASBase(sceneData, gid, gtid);
}

[shader("mesh")]
[numthreads(cMaxVerticesPerMeshlet, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload InstancedPayload payload,
    OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    let mesh = sceneData.meshBuffer.Get(payload.instance.meshInstance);

    // The amplification shader dispatched this submesh's meshlets, so gid is the
    // local meshlet index within it.
    let submesh = sceneData.submeshBuffer.Get(mesh.submeshes, payload.instance.submeshIndex);
    let meshlet = sceneData.meshletBuffer.Get(submesh.meshlets, gid);

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    EvaluateVertices(mesh, submesh, meshlet, gtid, verts);
    EvaluatePrimitives(submesh, meshlet, gtid, tris);
}
