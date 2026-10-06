// One terrain as its stage and every sampler of it read it: where the heightfield lies in the
// world, how its unorm samples decode to heights, how it is cut into levels of detail, and the
// material its surface shades through. One per IScene::CreateTerrain. The samples themselves are
// a texture bound beside this record.
public struct Terrain
{
    // World position of sample (0, 0) at the minimum height in xyz, and the cell size in w.
    public float4 originAndCellSize;

    // x: the height a sample of 1.0 adds to the origin's y. y: the size on screen a cell is drawn
    // at, in pixels, which chooses the level of detail. z, w: unused.
    public float4 heightRangeAndPixelsPerCell;

    public uint samplesX;
    public uint samplesZ;

    // Quadtree levels of patches, level 0 at the samples' own resolution and each above doubling
    // the cell. The coarsest level holds one node across the longer axis.
    public uint levels;

    public uint materialOffset;
};
