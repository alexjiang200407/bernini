// A bounding sphere over a run of cMeshletsPerGroup consecutive meshlets of one submesh, enclosing
// every vertex every one of them draws. Fitted at cook time to the vertices themselves rather than
// folded out of the meshlet spheres, which would carry a meshlet radius of slack at every extreme.
public struct MeshletGroup
{
    // xyz is the centre, w the radius -- local space, like Meshlet's.
    public float4 boundingSphere;
};
