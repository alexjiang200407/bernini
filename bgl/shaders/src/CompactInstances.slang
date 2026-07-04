import types.ComputeBuffer;
import types.SubmeshInstance;
import types.PackedBuffer;
import types.PsoType;
import idl.DispatchArgs;
import idl.Constants;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<uint> psoPrefixSum;
    ComputeBuffer<uint> compactedInstances;
    ComputeBuffer<DispatchArgs> dispatchArgs;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cCompactGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let psoType = uint(gUniforms.instanceBuffer[globalIdx].psoType);
    if (psoType >= kPsoCount)
    {
        return;
    }

    let base = (psoType == 0u) ? 0u : gUniforms.psoPrefixSum[psoType - 1u];

    uint slot;
    InterlockedAdd(gUniforms.dispatchArgs[psoType].threadCountX, 1u, slot);

    gUniforms.compactedInstances[base + slot] = globalIdx;
}
