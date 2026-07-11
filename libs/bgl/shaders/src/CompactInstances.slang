import types.ComputeBuffer;
import types.SubmeshInstance;
import types.PackedBuffer;
import types.EntryBuffer;
import types.RangeBuffer;
import idl.PsoType;
import idl.Mesh;
import idl.Submesh;
import idl.DispatchArgs;
import idl.Constants;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<uint> psoPrefixSum;
    ComputeBuffer<uint> compactedInstances;
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let instance = gUniforms.instanceBuffer[globalIdx];
    if (instance.meshInstance.Null())
    {
        return;
    }

    let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);
    let submesh = gUniforms.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);
    let psoType = submesh.pso;
    if (psoType >= kPsoCount)
    {
        return;
    }

    let base = (psoType == 0u) ? 0u : gUniforms.psoPrefixSum[psoType - 1u];

    uint slot;
    InterlockedAdd(gUniforms.dispatchArgs[psoType].threadCountX, 1u, slot);

    gUniforms.compactedInstances[base + slot] = globalIdx;
}
