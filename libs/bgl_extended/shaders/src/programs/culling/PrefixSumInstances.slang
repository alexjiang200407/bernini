import idl.Bucket;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<uint> inOutBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gScan[cMaxBuckets];

// One thread group, one thread per bucket: cMaxBuckets is this scan's hard ceiling.
[shader("compute")]
[numthreads(cMaxBuckets, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    gScan[idx] = gUniforms.inOutBuffer[idx];
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < cMaxBuckets; offset <<= 1u)
    {
        uint addend = (idx >= offset) ? gScan[idx - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gScan[idx] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    gUniforms.inOutBuffer[idx] = gScan[idx];
}
