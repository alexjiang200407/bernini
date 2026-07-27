import forward.ExpansionData;
import idl.Constants;
import idl.DispatchArgs;
import idl.DrawIndirectArgs;
import idl.Mesh;
import idl.MeshletInstance;
import idl.Submesh;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import types.RangeBuffer;
import types.SubmeshInstance;

// What the amplification stage does on a backend that has one: turn each visible instance of a PSO
// bucket into one record per meshlet, which the vertex-pulling draw covers with a fixed
// cVerticesPerMeshletRecord vertices each.
//
// drawArgs.vertexCount is the allocator as well as the output. A thread reserves its records by
// adding their vertices to it and divides the returned offset back down to a record slot, so the
// count the draw consumes is the same number that placed the records -- nothing to finalize, and
// nothing that can disagree.
//
// firstVertex does the same double duty: it is where the bucket's records start *and* the offset
// drawIndirect adds to SV_VertexID, so the vertex shader's `vertexId / cVerticesPerMeshletRecord`
// lands on the record this kernel wrote with no second base to keep in step.

struct ExpandData
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    RangeBuffer<Submesh> submeshBuffer;

    ComputeBuffer<MeshletInstance> meshletInstances;
    ComputeBuffer<DrawIndirectArgs> drawArgs;

    // Instances per bucket, as the counting sort left them. The dispatch is sized from the same
    // number, so the tail of the last group is out of range and must return.
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<ExpandData> gExpand;

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let pso = expansionData.psoIndex;

    if (globalIdx >= gExpand.dispatchArgs[pso].threadCountX.load())
    {
        return;
    }

    let instanceIdx = expansionData.compactedInstances[ResolveInstanceBase() + globalIdx];
    let instance = gExpand.instanceBuffer.Get(instanceIdx);
    let mesh = gExpand.meshBuffer.Get(instance.meshInstance);
    let submesh = gExpand.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    let meshletCount = submesh.GetMeshletCount();
    if (meshletCount == 0u)
    {
        return;
    }

    let vertexBase = gExpand.drawArgs[pso].vertexCount.add(meshletCount * cVerticesPerMeshletRecord);
    let slot = (gExpand.drawArgs[pso].firstVertex + vertexBase) / cVerticesPerMeshletRecord;

    for (uint m = 0u; m < meshletCount; ++m)
    {
        MeshletInstance record;
        record.instanceIndex = instanceIdx;
        record.meshletIndex = m;

        gExpand.meshletInstances[slot + m] = record;
    }
}
