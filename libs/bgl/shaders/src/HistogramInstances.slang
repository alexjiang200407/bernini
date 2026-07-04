import types.SubmeshInstance;
import types.ComputeBuffer;
import types.PackedBuffer;
import types.PsoType;
import idl.Constants;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gLocalHistogram[kPsoCount];

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    if (localIdx < kPsoCount)
    {
        gLocalHistogram[localIdx] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    let psoType = uint(gUniforms.instanceBuffer[globalIdx].psoType);
    if (psoType < kPsoCount)
    {
        InterlockedAdd(gLocalHistogram[psoType], 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localIdx < kPsoCount)
    {
        InterlockedAdd(gUniforms.outBuffer[localIdx], gLocalHistogram[localIdx]);
    }
}
