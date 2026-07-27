import types.SubmeshInstance;
import forward.ExpansionData;
import forward.ForwardData;
import forward.common;
import idl.BaseTable;
import idl.Constants;
import idl.Mesh;
import idl.Submesh;

public void ASBase(
    uint gid: SV_GroupID,       // Instance number
    uint gtid: SV_GroupThreadID // Local thread index within the group
)
{
    // The opaque base is the exclusive prefix sum of the prior PSOs' counts; the transparent one is
    // the partition's start in the depth-sorted list.
    uint base;
    if (expansionData.baseTable == BaseTable::kDepthSorted)
    {
        base = expansionData.transparentPartitionBase[expansionData.partitionIndex];
    }
    else
    {
        base = (expansionData.psoIndex == 0u) ? 0u : expansionData.psoPrefixSum[expansionData.psoIndex - 1u];
    }

    uint drawInstanceIdx = expansionData.compactedInstances[base + gid];
    let instance = forwardData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = forwardData.meshBuffer.Get(instance.meshInstance);

    // Each instance is one submesh; dispatch only that submesh's meshlets.
    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}
