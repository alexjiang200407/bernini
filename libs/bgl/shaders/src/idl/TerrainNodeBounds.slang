// The lowest and highest world y a node of a terrain's quadtree reaches: what the terrain stage
// culls the node by and measures its distance from. One per node of every level, level-major, a
// range each terrain owns.
public struct TerrainNodeBounds
{
    public float minY;
    public float maxY;
};
