import idl.PsoType;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<uint> inOutBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gScan[cMaxPsoBuckets];

// One thread group, one thread per bucket: cMaxPsoBuckets is this scan's hard ceiling.
[shader("compute")]
[numthreads(cMaxPsoBuckets, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    gScan[idx] = gUniforms.inOutBuffer[idx];
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < cMaxPsoBuckets; offset <<= 1u)
    {
        uint addend = (idx >= offset) ? gScan[idx - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gScan[idx] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    gUniforms.inOutBuffer[idx] = gScan[idx];
}
