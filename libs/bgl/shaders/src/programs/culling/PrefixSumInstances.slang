import idl.DrawBucket;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<uint> inOutBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gScan[cMaxDrawLanes];

// One thread group, one thread per draw lane: cMaxDrawLanes is this scan's hard ceiling.
[shader("compute")]
[numthreads(cMaxDrawLanes, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    gScan[idx] = gUniforms.inOutBuffer[idx];
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 1u; offset < cMaxDrawLanes; offset <<= 1u)
    {
        uint addend = (idx >= offset) ? gScan[idx - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gScan[idx] += addend;
        GroupMemoryBarrierWithGroupSync();
    }

    gUniforms.inOutBuffer[idx] = gScan[idx];
}
