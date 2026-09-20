public struct CullStats
{
    public Atomic<uint> tested;
    public Atomic<uint> frustumCulled;
    public Atomic<uint> meshletsTested;
    public Atomic<uint> meshletsCulled;
}
