import crowd.idl.Agent;
import crowd.idl.Group;
import crowd.idl.GroupSum;
import lib.types.ComputeBuffer;
import lib.types.UploadBuffer;

// One thread group per group row: each thread sums a fixed stride of the group's agents, then the
// sums are halved in a fixed order, so the same agents give the same bits on every run. No atomics:
// D3D12 has no float ones, and their order is the scheduler's.
struct Uniforms
{
    UploadBuffer<Group> groups;
    ComputeBuffer<Agent> agents;
    ComputeBuffer<GroupSum> sums;
};

ConstantBuffer<Uniforms> gUniforms;

static const uint c_Threads = 64u;

groupshared float2 gPositions[c_Threads];
groupshared float2 gFacings[c_Threads];

[shader("compute")]
[numthreads(c_Threads, 1, 1)]
void main(uint3 groupId: SV_GroupID, uint3 threadId: SV_GroupThreadID)
{
    let row = groupId.x;
    let thread = threadId.x;
    let group = gUniforms.groups[row];

    var position = float2(0.0f);
    var facing = float2(0.0f);
    for (uint i = thread; i < group.agentCount; i += c_Threads)
    {
        let agent = gUniforms.agents[group.firstAgent + i];
        position += agent.position;
        facing += agent.facing;
    }
    gPositions[thread] = position;
    gFacings[thread] = facing;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = c_Threads / 2u; stride > 0u; stride /= 2u)
    {
        if (thread < stride)
        {
            gPositions[thread] += gPositions[thread + stride];
            gFacings[thread] += gFacings[thread + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (thread != 0u)
        return;

    let count = float(group.agentCount);
    let facingSum = gFacings[0];
    let facingLength = length(facingSum);

    var sum : GroupSum;
    sum.meanPosition = gPositions[0] / count;
    // Unit facings that cancel leave a mean too short to have a direction.
    sum.meanFacing = facingLength > 1e-3f * count ? facingSum / facingLength : group.front;
    sum.agentCount = group.agentCount;
    sum.reserved = 0u;
    gUniforms.sums[row] = sum;
}
