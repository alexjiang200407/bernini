// The static tier's vertex evaluation: the bind-pose vertex bytes placed by the instance transform.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: Forward_StaticMesh, which draws the PSO-bucketed phase, and
// Forward_AnyMesh, which draws lists that mix tiers.

import types.SubmeshInstance;
import forward.common;
import forward.mesh_stage;
import forward.vertexdecode;
import idl.PsoType;
import idl.Constants;

public ForwardVSOut StaticVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    float4 worldPos = mul(ctx.mesh.transform, float4(vertex.position, 1.0));

    // The same world position through both cameras: an instance transform is fixed for its lifetime,
    // so the camera is the whole of a static surface's motion. A movable instance would carry its own
    // previous transform here.
    ForwardVSOut vertexOut;
    ProjectVertex(vertexOut, worldPos, worldPos);

    vertexOut.normal = mul((float3x3)ctx.mesh.transform, vertex.normal);
    vertexOut.uv = vertex.uv;
    vertexOut.tangent = float4(mul((float3x3)ctx.mesh.transform, vertex.tangent.xyz), vertex.tangent.w);

    vertexOut.materialIndex = instance.material.GetOffset();
    vertexOut.materialIsLoose = IsLoosePso(PsoType(instance.pso)) ? 1u : 0u;
    vertexOut.rimIntensity = ctx.mesh.rimIntensity;

    return vertexOut;
}
