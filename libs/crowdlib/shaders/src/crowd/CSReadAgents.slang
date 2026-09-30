import crowd.idl.Agent;
import crowd.idl.AgentReadback;
import crowd.idl.TickParams;
import lib.types.ComputeBuffer;

// Packs each agent's position and facing for ICrowd::ReadDebugAgents, in the tick's layout.
struct Uniforms
{
    TickParams params;
    ComputeBuffer<Agent> agents;
    ComputeBuffer<AgentReadback> readback;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let index = id.x;
    if (index >= gUniforms.params.agentCount)
        return;

    let agent = gUniforms.agents[index];
    var packed : AgentReadback;
    packed.position = agent.position;
    packed.facing = agent.facing;
    gUniforms.readback[index] = packed;
}
