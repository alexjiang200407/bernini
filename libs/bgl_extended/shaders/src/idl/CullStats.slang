public struct CullStats
{
    public Atomic<uint> tested;
    public Atomic<uint> frustumCulled;
    public Atomic<uint> meshletGroupsTested;
    public Atomic<uint> meshletGroupsCulled;
    public Atomic<uint> grassChunksTested;
    public Atomic<uint> grassChunksCulled;
    public Atomic<uint> grassBladesEmitted;
}
