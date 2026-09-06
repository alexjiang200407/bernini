// The static tier's vertex evaluation: the bind-pose vertex bytes placed by the instance transform.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: programs/forward/StaticMesh, which draws the PSO-bucketed phase,
// and programs/forward/AnyMesh, which draws lists that mix tiers.

import lib.forward.common;
import lib.forward.mesh_stage;
import lib.geom.vertexdecode;
import lib.data.SubmeshInstance;

public ForwardVSOut StaticVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    float4 worldPos = float4(ctx.mesh.TransformPoint(vertex.position), 1.0);

    // The bind-pose vertex placed by each frame's transform. The two are the same matrix on an
    // instance nobody moved, so a still surface's motion is the camera's alone.
    float4 prevWorldPos = float4(ctx.mesh.PrevTransformPoint(vertex.position), 1.0);

    ForwardVSOut vertexOut;
    ProjectVertex(vertexOut, worldPos, prevWorldPos);

    vertexOut.normal = ctx.mesh.TransformDirection(vertex.normal);
    vertexOut.uv = vertex.uv;
    vertexOut.tangent = float4(ctx.mesh.TransformDirection(vertex.tangent.xyz), vertex.tangent.w);

    vertexOut.materialOffset = instance.material.GetByteOffset();

    return vertexOut;
}
