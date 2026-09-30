/**
 * One group as a tick simulates it: its orders, resolved against its agent type, and the range of
 * the agent buffer its agents occupy.
 */
public struct Group
{
    public float2 goal;

    // The orders' facing, normalised.
    public float2 front;

    public uint firstAgent;
    public uint agentCount;

    public uint frontage;
    public float spacing;

    // The type's preferred speed times the orders' pace, and the type's maximum speed.
    public float speed;
    public float maxSpeed;
};
