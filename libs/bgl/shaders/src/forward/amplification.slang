import types.SubmeshInstance;
import forward.ExpansionData;
import forward.ForwardData;
import forward.common;
import idl.Constants;
import idl.Mesh;
import idl.Submesh;

public void ASBase(
    ConstantBuffer<ForwardData> sceneData,
    ConstantBuffer<ExpansionData> expansion,
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    // The opaque path is bucketed by PSO, so its base is the exclusive prefix sum of prior PSO
    // counts. The transparent path is a separate depth-sorted list split by occlude class, so its
    // base is the partition's start. Both are GPU values the CPU cannot know at record time.
    uint base;
    if (expansion.baseSource == cBaseSourceTransparentPartition)
    {
        base = expansion.transparentPartitionBase[expansion.partitionIndex];
    }
    else
    {
        base = (expansion.psoIndex == 0u) ? 0u : expansion.psoPrefixSum[expansion.psoIndex - 1u];
    }

    uint drawInstanceIdx = expansion.compactedInstances[base + gid];
    let instance = sceneData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = sceneData.meshBuffer.Get(instance.meshInstance);

    // Each instance is one submesh; dispatch only that submesh's meshlets.
    let submesh = sceneData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}
