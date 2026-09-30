/**
 * One group's row in a tick's reports, reduced in a fixed order so the same agents give the same
 * bits. `meanFacing` is normalised, and the group's front when its agents' facings cancel out.
 */
public struct GroupSum
{
    public float2 meanPosition;
    public float2 meanFacing;
    public uint agentCount;
    public uint reserved;
};
