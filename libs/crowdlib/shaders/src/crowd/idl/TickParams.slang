/** What every kernel of one tick reads. */
public struct TickParams
{
    public float tickSeconds;

    // SolverDesc::velocityInertia: the share of an agent's last velocity kept when it re-plans.
    public float velocityInertia;

    public uint agentCount;
    public uint groupCount;
    public uint agentRangeCount;
};
