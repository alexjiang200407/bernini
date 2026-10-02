/**
 * A range of one tick's agents and where they come from: `agentCount` agents written from
 * `firstAgent` on, read from the previous tick's buffer from `sourceFirstAgent` on, or spawned in
 * their slots when it is c_SpawnSource. They take slots `firstSlot` onwards of `group`. A tick's
 * ranges cover its agents exactly, in order, so the whole layout moves by O(groups) records and
 * never by a record per agent.
 */
public struct AgentRange
{
    public uint firstAgent;
    public uint sourceFirstAgent;
    public uint agentCount;
    public uint group;
    public uint firstSlot;

    // The render record of the agent at sourceFirstAgent in the previous tick, or c_SpawnSource.
    public uint sourceFirstRecord;
};
