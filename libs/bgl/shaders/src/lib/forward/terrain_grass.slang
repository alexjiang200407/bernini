import idl.Constants;
import idl.Terrain;
import idl.TerrainGrass;
import idl.TerrainNodeBounds;
import lib.forward.grass;
import lib.math.hash;
import lib.terrain.heightfield;
import lib.terrain.nodes;

// Grass on a terrain, built rather than stored: a layer covers the field with square tiles of
// cTerrainGrassClumpsAcross^2 clumps, each tile one chunk of the grass stage, and the stage reads
// only the tiles in a window around the camera. A clump stands jittered within its cell on the
// heightfield, and the layer's rules scale it by where it stands. Tiles are counted from the
// terrain's origin, so a clump is the same clump wherever the window has moved to.

float2 FieldSize(Terrain terrain)
{
    return float2(terrain.samplesX - 1u, terrain.samplesZ - 1u) * terrain.originAndCellSize.w;
}

// The tile slot `slot` of a `windowTiles`-square window centred on the camera's tile names, in tiles
// from the terrain's origin; false when it lies off the field.
public bool TerrainGrassTile(TerrainGrass layer, Terrain terrain, float3 cameraPos, uint windowTiles, uint slot, out int2 tile)
{
    let centre = int2(floor((cameraPos.xz - terrain.originAndCellSize.xz) / layer.tileSize));
    let corner = centre - int(windowTiles / 2u);
    tile = corner + int2(int(slot % windowTiles), int(slot / windowTiles));
    let tiles = int2(ceil(FieldSize(terrain) / layer.tileSize));
    return all(tile >= int2(0, 0)) && all(tile < tiles);
}

// The world box a tile's ground lies in: its square on the field, between the lowest and highest
// heights of the nodes it overlaps on the finest level whose nodes are no smaller than a tile.
public void TerrainGrassTileBox(
    TerrainGrass layer,
    Terrain terrain,
    StructuredBuffer<TerrainNodeBounds, ScalarDataLayout>.Handle nodeBounds,
    int2 tile,
    out float3 lo,
    out float3 hi
)
{
    let cellSize = terrain.originAndCellSize.w;
    let origin = terrain.originAndCellSize.xz;
    let tileLo = origin + float2(tile) * layer.tileSize;
    let tileHi = min(tileLo + layer.tileSize, origin + FieldSize(terrain));

    uint level = 0u;
    while (level + 1u < terrain.levels && float(NodeCells(level)) * cellSize < layer.tileSize)
    {
        ++level;
    }
    let nodeSize = float(NodeCells(level)) * cellSize;
    let across = uint2(NodesAcross(terrain.samplesX, level), NodesAcross(terrain.samplesZ, level));
    let first = layer.nodeBounds.GetStart() + LevelFirst(terrain, level);
    let lastNode = across - 1u;
    let n0 = min(uint2((tileLo - origin) / nodeSize), lastNode);
    let n1 = min(uint2((tileHi - origin) / nodeSize), lastNode);

    var minY = 1e30;
    var maxY = -1e30;
    for (uint z = n0.y; z <= n1.y; ++z)
    {
        for (uint x = n0.x; x <= n1.x; ++x)
        {
            let bound = nodeBounds[first + z * across.x + x];
            minY = min(minY, bound.minY);
            maxY = max(maxY, bound.maxY);
        }
    }
    lo = float3(tileLo.x, minY, tileLo.y);
    hi = float3(tileHi.x, maxY, tileHi.y);
}

// How much of the ground the layer's patches cover at `xz`: a low-frequency noise against the
// coverage, with an edge the layer's patchEdge wide so a patch shortens out rather than ending on a
// line.
float PatchGrowth(TerrainGrass layer, float2 xz)
{
    if (layer.patchCoverage >= 1.0)
    {
        return 1.0;
    }
    if (layer.patchCoverage <= 0.0)
    {
        return 0.0;
    }
    let p = xz / layer.patchSize + float(layer.seed) * float2(37.1, 17.3);
    let noise = 0.65 * ValueNoise(p) + 0.35 * ValueNoise(p * 2.03 + 19.1);
    let threshold = 1.0 - layer.patchCoverage;
    return smoothstep(threshold - layer.patchEdge, threshold + layer.patchEdge, noise);
}

// How much of the ground at `xz` the terrain's surface covers, as the Ground Color pass wrote it
// into the view's ground-cover texture over `rect` (GrassData::groundColorRect): 1 for a layer that
// does not follow its ground, and for a view with no texture.
public float TerrainGroundCover(TerrainGrass layer, Texture2D.Handle cover, SamplerState.Handle clampSampler, float4 rect, float2 xz)
{
    if (layer.groundCover == 0u || rect.w <= 0.0)
    {
        return 1.0;
    }
    let uv = (xz - rect.xy) * rect.z;
    return cover.SampleLevel(clampSampler, uv, 0.0).r;
}

// How tall a clump standing at `position` on ground facing `normal` grows, as a share of the look's
// heights: the slope rule, the height rule and the patches, multiplied.
public float TerrainGrassGrowth(TerrainGrass layer, float3 position, float3 normal)
{
    let slopeRange = max(layer.fullSlopeCos - layer.noneSlopeCos, 1e-6);
    let slope = saturate((normal.y - layer.noneSlopeCos) / slopeRange);
    let blend = max(layer.heightBlend, 1e-6);
    let height = saturate((position.y - layer.minHeight) / blend) * saturate((layer.maxHeight - position.y) / blend);
    return slope * height * PatchGrowth(layer, position.xz);
}

// Clump `k` of a tile, in world space: jittered within its cell, standing on the heightfield and
// facing its normal, scaled by the layer's rules and the ground's cover -- to nothing past the
// field's far edges.
public ClumpPoint TerrainGrassClump(
    TerrainGrass layer,
    Terrain terrain,
    Texture2D.Handle heights,
    Texture2D.Handle groundCover,
    SamplerState.Handle clampSampler,
    float4 groundRect,
    int2 tile,
    uint k
)
{
    let tileXMix = 0x8da6b343u;
    let tileZMix = 0xd8163841u;
    let layerMix = 0xcb1ab31fu;
    let tileSeed = HashUint((uint(tile.x) * tileXMix) ^ (uint(tile.y) * tileZMix) ^ (layer.seed * layerMix));
    let seed = HashUint(tileSeed + k);
    let jitter = float2(HashFloat(seed + 1u), HashFloat(seed + 2u));
    let cell = float2(k % cTerrainGrassClumpsAcross, k / cTerrainGrassClumpsAcross);
    let spacing = layer.tileSize / float(cTerrainGrassClumpsAcross);
    let origin = terrain.originAndCellSize.xz;
    let xz = origin + float2(tile) * layer.tileSize + (cell + jitter) * spacing;

    ClumpPoint clump;
    clump.position = float3(xz.x, TerrainHeightAt(terrain, heights, clampSampler, xz), xz.y);
    clump.normal = TerrainNormalAt(terrain, heights, clampSampler, xz);
    clump.color = float3(1.0, 1.0, 1.0);
    clump.seed = seed;

    let onField = all(xz <= origin + FieldSize(terrain));
    clump.heightScale = onField ? TerrainGrassGrowth(layer, clump.position, clump.normal) *
                                      TerrainGroundCover(layer, groundCover, clampSampler, groundRect, xz)
                                : 0.0;
    return clump;
}
