/**
 * One agent as a renderer reads it out of a crowd's render ring (ICrowd::GetRenderRing): where it
 * stands and the unit direction it faces, world (x, z); `source`, its index among the previous
 * tick's records, or c_RenderSpawned when it did not exist then; and `type`, its index in
 * CrowdDesc::agentTypes. Following `source` is what keeps an agent's motion across a split, a merge
 * or a destroy, which move it to another index. Generated into the public <crowdlib/RenderAgent.h>,
 * so its layout must agree on every backend: two float2s and two uints, 24 bytes on both.
 */
public struct RenderAgent
{
    public float2 position;
    public float2 facing;
    public uint source;
    public uint type;
};

// RenderAgent::source for an agent the previous tick did not have.
public static const uint c_RenderSpawned = 0xFFFFFFFFu;
