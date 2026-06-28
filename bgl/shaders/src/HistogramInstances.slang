import types.BaseInstance;
import types.ComputeBuffer;
import types.PackedBuffer;
import types.PsoType;

static const uint kPsoCount = uint(PsoType::kCount);
static const uint kThreadsPerGroup = 256;

struct Uniforms
{
    PackedBuffer<BaseInstance> instanceBuffer;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gLocalHistogram[kPsoCount];

[shader("compute")]
[numthreads(kThreadsPerGroup, 1, 1)]
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
