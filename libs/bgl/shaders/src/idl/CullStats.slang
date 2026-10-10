public struct CullStats
{
    public Atomic<uint> tested;
    public Atomic<uint> frustumCulled;
    // Occlusion decisions phase 1 left to phase 2, and those phase 2 then culled against the fresh
    // HZB: a placement's mesh and its impostor are each a decision of their own.
    public Atomic<uint> occlusionCandidates;
    public Atomic<uint> occlusionCulled;
    public Atomic<uint> meshletGroupsTested;
    public Atomic<uint> meshletGroupsCulled;
    public Atomic<uint> grassChunksTested;
    public Atomic<uint> grassChunksCulled;
    public Atomic<uint> grassBladesEmitted;
    public Atomic<uint> grassMeshGroups;
}
