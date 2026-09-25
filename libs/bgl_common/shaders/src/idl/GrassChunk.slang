// One run of at most cGrassClumpsPerChunk clumps of a field, as uploaded by AttachGrass: the unit the
// grass stage's amplification group culls and thins. The sphere encloses the clump positions in the
// mesh's space only; the stage inflates it by the look's reach.
public struct GrassChunk
{
    public float4 boundingSphere;
    public uint firstClump; // into the scene's grass clump buffer
    public uint clumpCount;
    public float maxHeightScale;
    public uint pad;
};
