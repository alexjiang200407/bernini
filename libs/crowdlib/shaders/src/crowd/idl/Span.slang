/**
 * Where a run of one tick's agents comes from: `agentCount` agents written from `firstAgent` on,
 * read from the previous tick's buffer from `source` on, or spawned in their slots when `source` is
 * c_SpawnSource. They take slots `firstSlot` onwards of `group`. A tick's spans cover its agents
 * exactly, in order, so the whole layout moves by O(groups) records and never by a record per agent.
 */
public struct Span
{
    public uint firstAgent;
    public uint source;
    public uint agentCount;
    public uint group;
    public uint firstSlot;
};
