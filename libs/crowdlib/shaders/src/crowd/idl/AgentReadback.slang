/**
 * One agent in ICrowd::ReadDebugAgents: where it stands and the unit direction it faces, world
 * (x, z). Generated into the public <crowdlib/debug/AgentReadback.h>, so its layout must agree on
 * every backend: two float2s, 16 bytes on both.
 */
public struct AgentReadback
{
    public float2 position;
    public float2 facing;
};
