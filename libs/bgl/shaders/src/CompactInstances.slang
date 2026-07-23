import types.ComputeBuffer;
import types.SubmeshInstance;
import types.PackedBuffer;
import idl.PsoType;
import idl.DispatchArgs;
import idl.Constants;
import idl.InstanceVisibility;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
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

    let psoType = instance.pso;
    if (psoType >= kPsoCount)
    {
        return;
    }

    // Skip exactly what the histogram skipped, or the bucket bases and the scatter disagree.
    if (gUniforms.visibility[globalIdx].visible == 0u)
    {
        return;
    }

    let base = (psoType == 0u) ? 0u : gUniforms.psoPrefixSum[psoType - 1u];

    uint slot;
    InterlockedAdd(gUniforms.dispatchArgs[psoType].threadCountX, 1u, slot);

    gUniforms.compactedInstances[base + slot] = globalIdx;
}
