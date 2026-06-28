import idl.IMeshInstance;
import idl.IMaterial;
import types.BaseInstance;
import forward.ForwardData;

public struct InstancedPayload<T>
    where T : IMeshInstance
{
    public BaseInstance<T> instance;
}

public struct ForwardVSOut
{
    public float4 position : SV_Position;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD0;
};

public void ASBase<T>(
    ConstantBuffer<ForwardData<T>> sceneData,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
    where T : IMeshInstance
{
    // compactedInstances is bucketed by PSO; this indirect dispatch only covers our bucket,
    // so offset SV_GroupID by the bucket's start (exclusive prefix sum of prior PSO counts).
    uint base = (sceneData.psoIndex == 0u) ? 0u : sceneData.psoPrefixSum[sceneData.psoIndex - 1u];
    uint drawInstanceIdx = sceneData.compactedInstances[base + gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let meshInstance = sceneData.meshBuffer.Get(instance.meshInstance);
    let mesh = sceneData.geomBuffer.Get(meshInstance.GetBaseMeshID());

    InstancedPayload<T> payload;
    payload.instance = instance;

    DispatchMesh(mesh.GetMeshletCount(), 1, 1, payload);
}
