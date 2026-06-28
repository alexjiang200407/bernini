import idl.IMeshInstance;
import idl.IMaterial;
import types.BaseInstance;
import gbuffer.GBufferData;

public struct InstancedPayload<T>
    where T : IMeshInstance
{
    public BaseInstance<T> instance;
}

public struct GBufferVSOut
{
    public float4 position : SV_Position;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD0;
};

public void ASBase<T>(
    ConstantBuffer<GBufferData<T>> sceneData,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
    where T : IMeshInstance
{
    uint drawInstanceIdx = sceneData.compactedInstances[gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let meshInstance = sceneData.meshBuffer.Get(instance.meshInstance);
    let mesh = sceneData.geomBuffer.Get(meshInstance.GetBaseMeshID());

    InstancedPayload<T> payload;
    payload.instance = instance;

    DispatchMesh(mesh.GetMeshletCount(), 1, 1, payload);
}
