import Entry;
import Range;
import Terrain;
import TerrainNodeBounds;

// One layer of grass on a terrain, as IScene::AttachTerrainGrass writes it: the terrain it grows on,
// the tiles of cGrassClumpsPerChunk clumps it is placed in, and the rules that scale each clump by
// where it stands (bgl::TerrainGrassDesc). Read by the grass stage alone.
public struct TerrainGrass
{
    public Entry<Terrain> terrain;
    public Range<TerrainNodeBounds> nodeBounds; // that terrain's, level-major
    public float tileSize;                      // cTerrainGrassClumpsAcross clumps a side
    public uint seed;                           // the layer's, so two layers on one terrain do not grow alike

    // The cosines of the steepest ground a clump stands at full height on and of the slope it
    // shrinks to nothing at: a ground normal's y between them scales it linearly.
    public float fullSlopeCos;
    public float noneSlopeCos;

    public float minHeight;
    public float maxHeight;
    public float heightBlend;

    public float patchSize;
    public float patchCoverage;

    public uint pad;
};
