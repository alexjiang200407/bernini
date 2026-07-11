import types.ComputeBuffer;
import idl.PsoType;

// Must be less than 1024
static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    ComputeBuffer<uint> inOutBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gScan[kPsoCount];

[shader("compute")]
[numthreads(kPsoCount, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    gScan[idx] = gUniforms.inOutBuffer[idx];
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < kPsoCount; offset <<= 1u)
    {
        uint addend = (idx >= offset) ? gScan[idx - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gScan[idx] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    gUniforms.inOutBuffer[idx] = gScan[idx];
}
