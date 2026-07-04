import types.SubmeshInstance;
import forward.ForwardData;
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
};

public void ASBase(
    ConstantBuffer<ForwardData> sceneData,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    // compactedInstances is bucketed by PSO; this indirect dispatch only covers our bucket,
    // so offset SV_GroupID by the bucket's start (exclusive prefix sum of prior PSO counts).
    uint base = (sceneData.psoIndex == 0u) ? 0u : sceneData.psoPrefixSum[sceneData.psoIndex - 1u];
    uint drawInstanceIdx = sceneData.compactedInstances[base + gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = sceneData.meshBuffer.Get(instance.meshInstance);

    // Each instance is one submesh; dispatch only that submesh's meshlets.
    let submesh = sceneData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}
