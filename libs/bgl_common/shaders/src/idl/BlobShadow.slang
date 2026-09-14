// One entry of a view's blob-shadow list: the placement shadowed, and the disc it casts on the
// ground plane. Dense and CPU-authored like the pose list -- the forward pass dispatches one
// group per entry.
public struct BlobShadow
{
    public uint mesh;
    public float radius;
    public float intensity;
    public float fadeHeight;
};
