/**
 * One agent as a renderer reads it out of a crowd's render ring (ICrowd::GetRenderRing): where it
 * stands and the unit direction it faces, world (x, z); `source`, its index among the previous
 * tick's records, or c_RenderSpawned when it did not exist then; `type`, its index in
 * CrowdDesc::agentTypes; and `id`, which it is given when it spawns and keeps for its life.
 * Following `source` is what keeps an agent's motion across a split, a merge or a destroy, which
 * move it to another index; `id` is what a reader derives anything from that must stay with the
 * agent across them, such as an animation phase. Ids are distinct among the agents alive at once
 * until the crowd has spawned 2^32. Generated into the public <crowdlib/RenderAgent.h>, so its
 * layout must agree on every backend: two float2s and four uints, 32 bytes on both, which is what
 * `pad` is for.
 */
public struct RenderAgent
{
    public float2 position;
    public float2 facing;
    public uint source;
    public uint type;
    public uint id;
    public uint pad;
};

// RenderAgent::source for an agent the previous tick did not have.
public static const uint c_RenderSpawned = 0xFFFFFFFFu;
