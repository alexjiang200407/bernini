import idl.Constants;
import idl.Terrain;

// A terrain's quadtree of patches as the GPU walks it: the twin of scene/terrain_lod.h, which lays
// out the node bounds every reader indexes, so a change here is a change there. A node at level `l`
// is a square of cTerrainPatchQuads << l cells; level 0 is the samples' own resolution.

public uint NodeCells(uint level)
{
    return cTerrainPatchQuads << level;
}

// Nodes of `level` across `samples` samples: the last may hang past the edge.
public uint NodesAcross(uint samples, uint level)
{
    let cells = NodeCells(level);
    return (samples - 1u + cells - 1u) / cells;
}

public struct Node
{
    public uint level;
    public uint x;
    public uint z;
};

// Where `level` starts in the level-major layout.
public uint LevelFirst(Terrain terrain, uint level)
{
    uint first = 0u;
    for (uint l = 0u; l < level; ++l)
    {
        first += NodesAcross(terrain.samplesX, l) * NodesAcross(terrain.samplesZ, l);
    }
    return first;
}

// The node at `index` of the level-major layout, and where its level starts.
public Node NodeAt(Terrain terrain, uint index, out uint levelFirst)
{
    Node node;
    node.level = terrain.levels;
    node.x = 0u;
    node.z = 0u;
    levelFirst = 0u;
    for (uint level = 0u; level < terrain.levels; ++level)
    {
        let across = NodesAcross(terrain.samplesX, level);
        let inLevel = across * NodesAcross(terrain.samplesZ, level);
        if (index < inLevel)
        {
            node.level = level;
            node.x = index % across;
            node.z = index / across;
            return node;
        }
        index -= inLevel;
        levelFirst += inLevel;
    }
    return node;
}

// The distance from `p` to the nearest point of the box, zero inside it.
public float NearestDistance(float3 lo, float3 hi, float3 p)
{
    let away = max(max(lo - p, float3(0.0)), p - hi);
    return length(away);
}

// Whether any of the box is inside every plane: the corner furthest along each normal decides.
public bool BoxInFrustum(float4 planes[6], float3 lo, float3 hi)
{
    for (uint i = 0u; i < 6u; ++i)
    {
        let plane = planes[i];
        let corner = float3(plane.x > 0.0 ? hi.x : lo.x, plane.y > 0.0 ? hi.y : lo.y, plane.z > 0.0 ? hi.z : lo.z);
        if (dot(plane.xyz, corner) + plane.w < 0.0)
        {
            return false;
        }
    }
    return true;
}
