import idl.Constants;
import idl.Terrain;
import idl.TerrainNodeBounds;
import lib.forward.common;
import lib.forward.mesh_stage;
import lib.forward.TerrainData;
import lib.geom.vertexdecode;
import lib.terrain.heightfield;

// The terrain geometry stage: one amplification thread per node of every level of a terrain's
// quadtree, each deciding alone whether it draws, then one mesh group per drawn node building its
// patch of cTerrainPatchQuads^2 quads from the height texture. The pixel side is the material's
// own opaque program, exactly as a static mesh with that material draws.
//
// The level rule and the node layout are scene/terrain_lod.h's, which the scene lays the node
// bounds out by and the tests check the rule on. See docs/terrain.md.

// --- the node layout, the twin of scene/terrain_lod.h ---

uint NodeCells(uint level)
{
    return cTerrainPatchQuads << level;
}

uint NodesAcross(uint samples, uint level)
{
    let cells = NodeCells(level);
    return (samples - 1u + cells - 1u) / cells;
}

struct Node
{
    uint level;
    uint x;
    uint z;
};

// The node at `index` of the level-major layout, and where its level starts.
Node NodeAt(Terrain terrain, uint index, out uint levelFirst)
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

// The distance at which a cell of `level` spans the terrain's pixels per cell on screen, floored
// at four times a node's diagonal so the morph has its zone and neighbours stay within a level.
float LevelRange(Terrain terrain, uint level)
{
    let cellSize = terrain.originAndCellSize.w;
    let cell = cellSize * float(1u << level);
    let pixelsPerCell = terrain.heightRangeAndPixelsPerCell.y * terrainData.lodPixelScale;
    let range = cell * terrainData.pixelsPerUnit / pixelsPerCell;
    let floorRange = 5.6568542 * cellSize * float(NodeCells(level));
    return max(range, floorRange);
}

// A node's world box: its cells on the ground, its lowest and highest height, and one of its
// cells of slack on the near sides, where an odd vertex morphs toward the even one before it.
void NodeBox(Terrain terrain, Node node, TerrainNodeBounds bound, out float3 lo, out float3 hi)
{
    let cells = NodeCells(node.level);
    let cellSize = terrain.originAndCellSize.w;
    let x0 = node.x * cells;
    let z0 = node.z * cells;
    let x1 = min(x0 + cells, terrain.samplesX - 1u);
    let z1 = min(z0 + cells, terrain.samplesZ - 1u);
    let slack = cellSize * float(1u << node.level);
    lo = float3(
        terrain.originAndCellSize.x + float(x0) * cellSize - slack, bound.lowest, terrain.originAndCellSize.z + float(z0) * cellSize - slack
    );
    hi = float3(terrain.originAndCellSize.x + float(x1) * cellSize, bound.highest, terrain.originAndCellSize.z + float(z1) * cellSize);
}

float NearestDistance(float3 lo, float3 hi, float3 p)
{
    let away = max(max(lo - p, float3(0.0)), p - hi);
    return length(away);
}

// Whether any of the box is inside every plane: the corner furthest along each normal decides.
bool BoxInFrustum(float4 planes[6], float3 lo, float3 hi)
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

// One node per amplification group, as the grass stage launches one chunk per group: a group
// of one thread decides and launches, so nothing is gathered across threads to launch from.
struct TerrainPayload
{
    uint node;
};

groupshared TerrainPayload gTerrainPayload;

[numthreads(1, 1, 1)]
[shader("amplification")]
void ASMain(uint3 gid: SV_GroupID)
{
    uint groups = 0u;

    let index = gid.y * terrainData.dispatchWidth + gid.x;
    if (index < terrainData.nodeCount)
    {
        let terrain = terrainData.terrains.Get(terrainData.terrain);
        uint levelFirst;
        let node = NodeAt(terrain, index, levelFirst);

        float3 lo;
        float3 hi;
        NodeBox(terrain, node, terrainData.nodeBounds[terrainData.firstNodeBound + index], lo, hi);

        if (BoxInFrustum(terrainData.frustumPlanes, lo, hi))
        {
            let distance = NearestDistance(lo, hi, terrainData.cameraPos);

            // A node draws at its level when it is no nearer than the finer level's range and its
            // parent is nearer than its own: the chain above every finest cell then draws once.
            var coarseEnough = node.level == 0u;
            if (!coarseEnough)
            {
                coarseEnough = distance >= LevelRange(terrain, node.level - 1u);
            }

            var fineEnough = node.level + 1u == terrain.levels;
            if (!fineEnough)
            {
                Node parent;
                parent.level = node.level + 1u;
                parent.x = node.x / 2u;
                parent.z = node.z / 2u;
                let parentFirst = levelFirst + NodesAcross(terrain.samplesX, node.level) * NodesAcross(terrain.samplesZ, node.level);
                let parentIndex = parentFirst + parent.z * NodesAcross(terrain.samplesX, parent.level) + parent.x;

                float3 parentLo;
                float3 parentHi;
                NodeBox(terrain, parent, terrainData.nodeBounds[terrainData.firstNodeBound + parentIndex], parentLo, parentHi);
                fineEnough = NearestDistance(parentLo, parentHi, terrainData.cameraPos) < LevelRange(terrain, node.level);
            }

            if (coarseEnough && fineEnough)
            {
                gTerrainPayload.node = index;
                groups = 1u;
            }
        }
    }

    DispatchMesh(groups, 1, 1, gTerrainPayload);
}

static const uint cVerticesAcross = cTerrainPatchQuads + 1u;

// How far along the morph a vertex at `distance` is: none up to three quarters of its level's
// range, all of it at the range, where a coarser neighbour's vertices are the even ones alone.
float MorphAt(float distance, float range)
{
    let start = 0.75 * range;
    return saturate((distance - start) / (range - start));
}

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload TerrainPayload launch,
    OutputVertices<ForwardVSOut, cTerrainPatchVertices> verts,
    OutputIndices<uint3, cTerrainPatchPrims> tris
)
{
    let terrain = terrainData.terrains.Get(terrainData.terrain);
    uint levelFirst;
    let node = NodeAt(terrain, launch.node, levelFirst);

    SetMeshOutputCounts(cTerrainPatchVertices, cTerrainPatchPrims);

    let step = 1u << node.level; // samples between the patch's vertices
    let cells = NodeCells(node.level);
    let coarsest = node.level + 1u == terrain.levels;
    let range = LevelRange(terrain, node.level);
    let fieldSize = float2(terrain.samplesX - 1u, terrain.samplesZ - 1u) * terrain.originAndCellSize.w;

    for (uint v = gtid; v < cTerrainPatchVertices; v += cMeshGroupSize)
    {
        let i = v % cVerticesAcross;
        let j = v / cVerticesAcross;

        // The vertex's sample, clamped to the field: a patch hanging past the edge folds onto it,
        // and a vertex on the edge stays there whatever its level, so the field's outline is the
        // same from every level and the morph below never pulls it in.
        let last = uint2(terrain.samplesX - 1u, terrain.samplesZ - 1u);
        let unclamped = uint2(node.x * cells + i * step, node.z * cells + j * step);
        let onEdge = float2(unclamped.x >= last.x ? 0.0 : 1.0, unclamped.y >= last.y ? 0.0 : 1.0);
        var sample = float2(min(unclamped, last));
        var xz = terrain.originAndCellSize.xz + sample * terrain.originAndCellSize.w;

        // A vertex at an odd index of its level is not on the coarser grid: as it nears the
        // level's range it slides onto the even vertex before it, so at the range the patch's
        // edge is the coarser neighbour's, vertex for vertex. The coarsest level has no coarser.
        if (!coarsest)
        {
            let height = TerrainHeightAt(terrain, terrainData.heights, terrainData.heightSampler, xz);
            let morph = MorphAt(distance(terrainData.cameraPos, float3(xz.x, height, xz.y)), range);
            let coarse = float(2u * step);
            sample -= frac(sample / coarse) * coarse * morph * onEdge;
            xz = terrain.originAndCellSize.xz + sample * terrain.originAndCellSize.w;
        }

        let height = TerrainHeightAt(terrain, terrainData.heights, terrainData.heightSampler, xz);
        let worldPos = float4(xz.x, height, xz.y, 1.0);

        float3 normal;
        float3 tangent;
        TerrainFrameAt(terrain, terrainData.heights, terrainData.heightSampler, xz, normal, tangent);

        ForwardVSOut vertex;
        ProjectVertex(vertex, worldPos, worldPos);
        vertex.normal = normal;
        vertex.uv = (xz - terrain.originAndCellSize.xz) / fieldSize;
        vertex.uv1 = cNoUv1;
        vertex.tangent = float4(tangent, 1.0);
        vertex.materialOffset = terrain.materialOffset;
        verts[v] = vertex;
    }

    // Two triangles a quad, wound so a face whose normal points up is front-facing: along +x then
    // toward -z, as IScene::AddPlaneGeom's plane is once laid flat.
    for (uint p = gtid; p < cTerrainPatchPrims; p += cMeshGroupSize)
    {
        let quad = p / 2u;
        let a = (quad / cTerrainPatchQuads) * cVerticesAcross + quad % cTerrainPatchQuads;
        let b = a + 1u;
        let c = a + cVerticesAcross + 1u;
        let d = a + cVerticesAcross;
        tris[p] = (p & 1u) == 0u ? uint3(a, c, b) : uint3(a, d, c);
    }
}
