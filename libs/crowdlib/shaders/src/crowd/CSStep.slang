import crowd.formation;
import crowd.idl.Agent;
import crowd.idl.AgentRange;
import crowd.idl.Constants;
import crowd.idl.Group;
import crowd.idl.RenderAgent;
import crowd.idl.TickParams;
import lib.types.ComputeBuffer;
import lib.types.UploadBuffer;

// One tick of one agent: laid out from the previous tick's buffer (or spawned in its slot) by the
// AgentRange covering it, then its velocity planned toward its slot and its position integrated.
// With a render ring, it also writes the agent's RenderAgent record into the tick's slot of it.
struct Uniforms
{
    TickParams params;
    UploadBuffer<Group> groups;
    UploadBuffer<AgentRange> ranges;
    ComputeBuffer<Agent> previous;
    ComputeBuffer<Agent> agents;
    ComputeBuffer<RenderAgent> renderRing;
};

ConstantBuffer<Uniforms> gUniforms;

// The range holding agent `index`: the last whose firstAgent is not past it. The ranges cover the
// agents in order, so a binary search finds it.
AgentRange FindRange(uint index)
{
    uint low = 0u;
    uint high = gUniforms.params.agentRangeCount - 1u;
    while (low < high)
    {
        let middle = (low + high + 1u) / 2u;
        if (gUniforms.ranges[middle].firstAgent <= index)
            low = middle;
        else
            high = middle - 1u;
    }
    return gUniforms.ranges[low];
}

Agent LayOut(uint index, AgentRange range, Group group)
{
    let offset = index - range.firstAgent;
    var agent : Agent;
    if (range.sourceFirstAgent == c_SpawnSource)
    {
        agent.slot = range.firstSlot + offset;
        agent.position = SlotPosition(group, agent.slot);
        agent.velocity = float2(0.0f);
        agent.facing = group.front;
    }
    else
    {
        agent = gUniforms.previous[range.sourceFirstAgent + offset];
        agent.slot = range.firstSlot + offset;
    }
    agent.group = range.group;
    return agent;
}

// Toward the slot at the group's speed, slowing so as not to pass it within the tick; blended with
// the last velocity by the solver's inertia and capped at the type's maximum speed.
float2 PlanVelocity(Agent agent, Group group)
{
    let toSlot = SlotPosition(group, agent.slot) - agent.position;
    let distance = length(toSlot);
    let tick = gUniforms.params.tickSeconds;
    let desired = distance > 0.0f ? toSlot * (min(group.speed, distance / tick) / distance) : float2(0.0f);

    let inertia = gUniforms.params.velocityInertia;
    var velocity = inertia * agent.velocity + (1.0f - inertia) * desired;
    let speed = length(velocity);
    if (speed > group.maxSpeed)
        velocity *= group.maxSpeed / speed;
    return velocity;
}

[shader("compute")]
[numthreads(c_ThreadsPerGroup, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let index = id.x;
    if (index >= gUniforms.params.agentCount)
        return;

    let range = FindRange(index);
    let group = gUniforms.groups[range.group];
    var agent = LayOut(index, range, group);

    agent.velocity = PlanVelocity(agent, group);
    agent.position += agent.velocity * gUniforms.params.tickSeconds;

    let speed = length(agent.velocity);
    agent.facing = speed > c_WalkingSpeedShare * group.speed ? agent.velocity / speed : group.front;

    gUniforms.agents[index] = agent;

    let renderFirst = gUniforms.params.renderFirstRecord;
    if (renderFirst != c_NoRenderRing)
    {
        // An agent's index in the previous tick is its record's index there too: both ticks wrote
        // their records in agent order.
        var record : RenderAgent;
        record.position = agent.position;
        record.facing = agent.facing;
        let offset = index - range.firstAgent;
        let spawned = range.sourceFirstAgent == c_SpawnSource;
        record.source = spawned ? c_RenderSpawned : range.sourceFirstAgent + offset;
        record.type = group.agentType;
        gUniforms.renderRing[renderFirst + index] = record;
    }
}
