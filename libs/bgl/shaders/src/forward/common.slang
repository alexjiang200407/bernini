import types.SubmeshInstance;
import forward.ForwardData;
import idl.Constants;
import idl.PsoType;
import idl.Mesh;
import idl.Submesh;

public struct InstancedPayload
{
    public SubmeshInstance instance;
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    public float3 worldPos : POSITION0;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD0;
    public float4 tangent : TANGENT;
    public nointerpolation uint materialIndex : MATERIAL;

    // Which material buffer materialIndex addresses. The transparent pipelines are shared by both
    // material types, so their pixel shader cannot know it from the PSO the way the others do.
    public nointerpolation uint materialIsLoose : MATERIALTYPE;
};

// A loose material stores its channels as routes rather than a baked triplet, so it reads from a
// different buffer. The PSO the instance resolved to is what says which.
public bool IsLoosePso(uint pso)
{
    return pso == uint(PsoType::kOpaque_StaticMesh_LoosePbr) || pso == uint(PsoType::kAlphaTest_StaticMesh_LoosePbr) ||
           pso == uint(PsoType::kTransparent_StaticMesh_LoosePbr) || pso == uint(PsoType::kTransparentOcclude_StaticMesh_LoosePbr);
}

public void ASBase(
    ConstantBuffer<ForwardData> sceneData,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    // The opaque path is bucketed by PSO, so its base is the exclusive prefix sum of prior PSO
    // counts. The transparent path is a separate depth-sorted list split by occlude class, so its
    // base is the partition's start. Both are GPU values the CPU cannot know at record time.
    uint base;
    if (sceneData.baseSource == cBaseSourceTransparentPartition)
    {
        base = sceneData.transparentPartitionBase[sceneData.partitionIndex];
    }
    else
    {
        base = (sceneData.psoIndex == 0u) ? 0u : sceneData.psoPrefixSum[sceneData.psoIndex - 1u];
    }

    uint drawInstanceIdx = sceneData.compactedInstances[base + gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = sceneData.meshBuffer.Get(instance.meshInstance);

    // Each instance is one submesh; dispatch only that submesh's meshlets.
    let submesh = sceneData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}
