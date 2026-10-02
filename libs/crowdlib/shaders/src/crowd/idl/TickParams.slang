/** What every kernel of one tick reads. */
public struct TickParams
{
    public float tickSeconds;

    // SolverDesc::velocityInertia: the share of an agent's last velocity kept when it re-plans.
    public float velocityInertia;

    public uint agentCount;
    public uint groupCount;
    public uint agentRangeCount;

    // The tick's first record in the render ring, or c_NoRenderRing for a crowd without one.
    public uint renderFirstRecord;
};
