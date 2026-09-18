import idl.DrawBucket;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<uint> inOutBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gScan[cMaxDrawBuckets];

// One thread group, one thread per bucket: cMaxDrawBuckets is this scan's hard ceiling.
[shader("compute")]
[numthreads(cMaxDrawBuckets, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    gScan[idx] = gUniforms.inOutBuffer[idx];
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < cMaxDrawBuckets; offset <<= 1u)
    {
        uint addend = (idx >= offset) ? gScan[idx - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gScan[idx] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    gUniforms.inOutBuffer[idx] = gScan[idx];
}
