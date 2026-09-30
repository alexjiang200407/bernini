/**
 * One agent between ticks, on the ground plane as world (x, z). A tick's agents are stored in group
 * order: each group's agents contiguous and in formation-slot order, so `slot` is also the agent's
 * offset in its group's range.
 */
public struct Agent
{
    public float2 position;
    public float2 velocity;

    // Unit length: the velocity's direction while the agent walks, its orders' facing once it stands.
    public float2 facing;

    // The agent's row in the tick's Group table.
    public uint group;
    public uint slot;
};
