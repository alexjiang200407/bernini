public struct CullStats
{
    public Atomic<uint> tested;
    public Atomic<uint> frustumCulled;
    public Atomic<uint> meshletGroupsTested;
    public Atomic<uint> meshletGroupsCulled;
}
