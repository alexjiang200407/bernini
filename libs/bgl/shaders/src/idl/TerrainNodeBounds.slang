// The lowest and highest height of one node of a terrain's quadtree, world space: what the terrain
// stage culls the node by and measures its distance from. One per node of every level, level-major,
// a range each terrain owns.
public struct TerrainNodeBounds
{
    public float lowest;
    public float highest;
};
