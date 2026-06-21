import intfc.IMeshInstance;
import intfc.IMaterial;
import db.BaseInstance;
import uniforms.SceneData;
import util.constants;
import db.StaticMesh;
import db.Meshlet;
import db.Vertex;
import gbuffer.common;

ConstantBuffer<SceneData<StaticMeshInstance>> sceneData;

void EvaluateVertices(
    StaticMeshInstance                                         instance,
    StaticGeom                                                 mesh,
    Meshlet                                                    meshlet,
    uint                                                       gtid,
    inout OutputVertices<GBufferVSOut, cMaxVerticesPerMeshlet> verts)
{
    if (gtid < meshlet.vertexCount)
    {
        uint vertexIdx = sceneData.vertexMapBuffer.Get(mesh.vertexMap, meshlet.relativeVertexOffset + gtid);
        Vertex v       = sceneData.vertexBuffer.Get(mesh.vertices, vertexIdx);

        float4 worldPos = mul(instance.transform, float4(v.pos, 1.0));
        
        GBufferVSOut vertexOut;
        vertexOut.position = mul(sceneData.viewProj, worldPos);
        vertexOut.normal   = mul((float3x3)instance.transform, v.normal);
        vertexOut.uv       = v.uv;

        verts[gtid] = vertexOut;
    }
}

void EvaluatePrimitives(
    StaticGeom                                      mesh,
    Meshlet                                         meshlet,
    uint                                            gtid,
    inout OutputIndices<uint3, cMaxPrimsPerMeshlet> tris)
{
    if (gtid < meshlet.triangleCount)
    {
        tris[gtid] = sceneData.indexBuffer.Get<3>(
            mesh.indices, meshlet.relativeIndexOffset + gtid * 3
        );
    }
}

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(
    uint gid  : SV_GroupID,        // Total Instances
    uint gtid : SV_GroupThreadID  // Local thread index within the group
) {
    ASBase(sceneData, gid, gtid);
}

[shader("mesh")]
[numthreads(cMaxVerticesPerMeshlet, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint                                                 gid  : SV_GroupID,
    uint                                                 gtid : SV_GroupThreadID,
    payload InstancedPayload<StaticMeshInstance>         payload,   
    OutputVertices<GBufferVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet>            tris
) {
    let meshInst = sceneData.meshBuffer.Get(payload.instance.meshInstance);
    let mesh = sceneData.geomBuffer.Get(meshInst.GetBaseMeshID());
    let meshlet = sceneData.meshletBuffer.Get(mesh.meshlets, gid);

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    EvaluateVertices(meshInst, mesh, meshlet, gtid, verts);
    EvaluatePrimitives(mesh, meshlet, gtid, tris);
}
