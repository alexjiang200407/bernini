import idl.Constants;
import idl.GrassChunk;
import idl.GrassChunkRef;
import idl.GrassClump;
import idl.GrassDraw;
import idl.GrassLook;
import idl.MeshInstance;
import idl.Terrain;
import idl.TerrainGrass;
import lib.forward.common;
import lib.forward.ExpansionData;
import lib.forward.grass;
import lib.forward.GrassData;
import lib.forward.grass_vertex;
import lib.forward.mesh_stage;
import lib.forward.terrain_grass;
import lib.math.frustum;
import lib.terrain.nodes;

// The grass geometry stage: one amplification group per chunk of a view's grass list, culled to the
// frustum and thinned by distance, launching mesh groups that each build a run of the chunk's blades.
// A chunk is a run of clumps a mesh stored, or a tile of clumps a terrain's layer builds around the
// camera (lib.forward.terrain_grass); past reading a clump, both are the same blades. The pixel side
// is the material's grass program (lib.forward.GrassShading), which lights the blade through the
// same entry Forward's programs do.

GrassWind ViewWind()
{
    GrassWind wind;
    wind.direction = grassData.windDirection;
    wind.strength = grassData.windStrength;
    wind.gustScale = grassData.windGustScale;
    wind.gustSpeed = grassData.windGustSpeed;
    wind.gustStrength = grassData.windGustStrength;
    return wind;
}

// A blade's shape carried into world space by the placement's transform this frame or last.
BladeShape PlacedShape(MeshInstance mesh, BladeShape shape, bool previous)
{
    let root = previous ? mesh.PrevTransformPoint(shape.root) : mesh.TransformPoint(shape.root);
    let top = shape.root + shape.up * shape.height;
    let ahead = shape.root + shape.facing;
    let upWorld = (previous ? mesh.PrevTransformPoint(top) : mesh.TransformPoint(top)) - root;
    let facingWorld = (previous ? mesh.PrevTransformPoint(ahead) : mesh.TransformPoint(ahead)) - root;

    BladeShape placed = shape;
    placed.root = root;
    placed.height = length(upWorld);
    placed.up = upWorld / placed.height;
    placed.facing = normalize(facingWorld);
    placed.side = cross(placed.up, placed.facing);
    return placed;
}

// The blade posed in world space at `time`, the wind blowing along the ground under it.
BladePose PoseAt(GrassLook look, GrassWind wind, BladeShape placed, float time)
{
    let along = float3(wind.direction.x, 0.0, wind.direction.y);
    let push = along * (placed.height * WindPush(look, wind, placed.root, placed.random, time));
    return PoseBlade(look, placed, push);
}

// Where a chunk reference's clumps come from, and the space they are in: a mesh's stored run,
// placed by its instance, or a terrain's tile, already in the world and still.
struct ChunkSource
{
    bool onTerrain;
    uint clumpCount;

    GrassChunk chunk;
    MeshInstance mesh;

    TerrainGrass layer;
    Terrain terrain;
    int2 tile;

    ClumpPoint Clump(uint clumpLocal)
    {
        if (onTerrain)
        {
            return TerrainGrassClump(layer, terrain, grassData.heights, grassData.heightSampler, tile, clumpLocal);
        }
        let index = chunk.firstClump + clumpLocal;
        return StoredClump(grassData.clumps[index], index);
    }

    float3 WorldPoint(float3 p) { return onTerrain ? p : mesh.TransformPoint(p); }

    BladeShape Placed(BladeShape shape, bool previous) { return onTerrain ? shape : PlacedShape(mesh, shape, previous); }
};

// The source `chunkRef` names; false for a terrain's tile off the field, which draws nothing.
// Inlined: called out of line, Slang 2026.7.1 passes a struct read from a buffer to the asserting accessors
// below it by a PhysicalStorageBuffer pointer, which SPIR-V rejects (docs/known_issues.md).
[ForceInline]
bool SourceOf(GrassChunkRef chunkRef, GrassDraw draw, out ChunkSource source)
{
    source = {};
    source.onTerrain = !draw.terrainGrass.Null();
    if (!source.onTerrain)
    {
        source.chunk = grassData.chunks[chunkRef.chunk];
        source.mesh = forwardData.meshBuffer.Get(draw.mesh);
        source.clumpCount = source.chunk.clumpCount;
        return true;
    }

    source.layer = grassData.terrainGrass.Get(draw.terrainGrass);
    source.terrain = grassData.terrains.Get(source.layer.terrain);
    source.clumpCount = cGrassClumpsPerChunk;
    return TerrainGrassTile(source.layer, source.terrain, grassData.cameraPos, draw.windowTiles, chunkRef.chunk, source.tile);
}

struct GrassPayload
{
    uint chunkRefIndex;
    uint keptBlades;
    uint segments;
    uint bladesPerGroup;
};

groupshared GrassPayload gGrassPayload;

[numthreads(1, 1, 1)]
[shader("amplification")]
void ASMain(uint3 gid: SV_GroupID)
{
    let refIndex = gid.y * grassData.dispatchWidth + gid.x;
    uint groups = 0u;

    ChunkSource source;
    if (refIndex < grassData.refCount)
    {
        let chunkRef = grassData.chunkRefs[grassData.firstRef + refIndex];
        let draw = grassData.draws[chunkRef.draw];
        let look = grassData.looks.Get(draw.look);
        let found = SourceOf(chunkRef, draw, source);

        // The nearest any blade of the chunk can be, and the largest any can be scaled.
        var visible = false;
        var nearest = 0.0;
        var maxHeightScale = 1.0;
        if (found && source.onTerrain)
        {
            float3 lo;
            float3 hi;
            TerrainGrassTileBox(source.layer, source.terrain, grassData.nodeBounds, source.tile, lo, hi);
            let reach = BladeReach(look, 1.0) + look.clumpRadius;
            lo -= reach;
            hi += reach;
            visible = BoxInFrustum(expansionData.cullView[0].frustumPlanes, lo, hi);
            nearest = NearestDistance(lo, hi, grassData.cameraPos);
        }
        else if (found)
        {
            let chunk = source.chunk;
            let local =
                float4(chunk.boundingSphere.xyz, chunk.boundingSphere.w + BladeReach(look, chunk.maxHeightScale) + look.clumpRadius);
            let world = source.mesh.TransformSphere(local);
            visible = SphereInFrustum(expansionData.cullView[0].frustumPlanes, world.xyz, world.w);
            nearest = max(0.0, distance(grassData.cameraPos, world.xyz) - world.w);
            maxHeightScale = chunk.maxHeightScale;
        }

#if defined(BERNINI_GPU_DEBUG)
        expansionData.stats[0].grassChunksTested.add(1u);
        if (!visible)
        {
            expansionData.stats[0].grassChunksCulled.add(1u);
        }
#endif

        if (visible)
        {
            // A chunk whose largest blade is too small to see where it is nearest launches nothing,
            // and otherwise every blade.
            let total = source.clumpCount * look.bladesPerClump;
            let tallest = look.maxHeight * maxHeightScale;
            let kept = BladeVisible(tallest * ThinningAt(look, nearest).scale, nearest, grassData.pixelsPerUnit) ? total : 0u;
            let segments = SegmentsAt(look, nearest, tallest, grassData.pixelsPerUnit);
            let perGroup = BladesPerGroup(segments, cGrassVerticesPerGroup, cGrassPrimsPerGroup);

            gGrassPayload.chunkRefIndex = refIndex;
            gGrassPayload.keptBlades = kept;
            gGrassPayload.segments = segments;
            gGrassPayload.bladesPerGroup = perGroup;
            groups = (kept + perGroup - 1u) / perGroup;
        }
    }

    DispatchMesh(groups, 1, 1, gGrassPayload);
}

groupshared uint gSurvivors[cGrassVerticesPerGroup];
groupshared uint gSurvivorCount;

// Each surviving blade posed once, by one thread, and read by every vertex along it.
struct PosedBlade
{
    BladePose now;
    BladePose before;
    float3 side;
    float3 sideBefore;
    float width;  // the root's, widened as the field thins
    float facing; // 1 or -1: which way the face turns toward the camera
    float3 ground;
    float groundBlend;
    float3 color;
    float random;
};
groupshared PosedBlade gPosed[cGrassVerticesPerGroup / 4u];

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload GrassPayload launch,
    OutputVertices<GrassVSOut, cGrassVerticesPerGroup> verts,
    OutputIndices<uint3, cGrassPrimsPerGroup> tris
)
{
    let chunkRef = grassData.chunkRefs[grassData.firstRef + launch.chunkRefIndex];
    let draw = grassData.draws[chunkRef.draw];
    let look = grassData.looks.Get(draw.look);
    ChunkSource source;
    SourceOf(chunkRef, draw, source);

    let first = gid * launch.bladesPerGroup;
    let last = min(launch.keptBlades, first + launch.bladesPerGroup);

    if (gtid == 0u)
    {
        gSurvivorCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // A blade too small to see where it stands is dropped; every other one is drawn, sized by its own
    // distance. The order survivors land in does not matter.
    let b = first + gtid;
    if (b < last)
    {
        uint clumpLocal;
        uint bladeInClump;
        BladeAddress(b, source.clumpCount, clumpLocal, bladeInClump);
        let clump = source.Clump(clumpLocal);
        let rootWorld = source.WorldPoint(clump.position);
        let away = distance(grassData.cameraPos, rootWorld);
        let tallest = look.maxHeight * clump.heightScale * ThinningAt(look, away).scale;
        if (BladeVisible(tallest, away, grassData.pixelsPerUnit))
        {
            uint slot;
            InterlockedAdd(gSurvivorCount, 1u, slot);
            gSurvivors[slot] = b;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    let survivors = gSurvivorCount;
    let segments = launch.segments;
    let vertsPerBlade = 2u * (segments + 1u);
    let trisPerBlade = 2u * segments;

    // One call site for the counts, whichever way that goes: DXC refuses a second.
    SetMeshOutputCounts(survivors * vertsPerBlade, survivors * trisPerBlade);

#if defined(BERNINI_GPU_DEBUG)
    if (gtid == 0u)
    {
        expansionData.stats[0].grassBladesEmitted.add(survivors);
    }
#endif

    if (gtid < survivors)
    {
        uint clumpLocal;
        uint bladeInClump;
        BladeAddress(gSurvivors[gtid], source.clumpCount, clumpLocal, bladeInClump);
        let clump = source.Clump(clumpLocal);
        var shape = MakeBladeShape(look, clump, bladeInClump);
        let wind = ViewWind();
        let rootWorld = source.WorldPoint(shape.root);
        let distanceToCamera = distance(grassData.cameraPos, rootWorld);
        let thinning = ThinningAt(look, distanceToCamera);
        // A survivor's scale is above zero; the floor keeps its placed height a length to divide by.
        let scale = max(thinning.scale, 1e-3);
        shape.height *= scale;
        let placed = source.Placed(shape, false);
        let placedBefore = source.Placed(shape, true);

        PosedBlade posed;
        posed.now = PoseAt(look, wind, placed, viewData.time);
        posed.before = PoseAt(look, wind, placedBefore, viewData.prevTime);
        posed.side = placed.side;
        posed.sideBefore = placedBefore.side;
        posed.width = look.rootWidth * thinning.widen * scale;

        // One side per blade, judged at its middle, so a bending blade never shades half from each.
        let middleFace = cross(placed.side, BezierTangent(posed.now, 0.5));
        posed.facing = dot(middleFace, grassData.cameraPos - BezierPoint(posed.now, 0.5)) < 0.0 ? -1.0 : 1.0;
        posed.ground = placed.up;
        posed.groundBlend = GroundNormalBlend(look, distanceToCamera);
        posed.color = BladeColor(look, clump, shape.random);
        posed.random = shape.random;
        gPosed[gtid] = posed;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint v = gtid; v < survivors * vertsPerBlade; v += cMeshGroupSize)
    {
        let posed = gPosed[v / vertsPerBlade];
        let row = (v % vertsPerBlade) / 2u;
        let edge = float((v % vertsPerBlade) & 1u);

        let t = float(row) / float(segments);
        let across = (edge - 0.5) * posed.width * lerp(1.0, look.tipWidth, t);
        let position = BezierPoint(posed.now, t) + posed.side * across;
        let positionBefore = BezierPoint(posed.before, t) + posed.sideBefore * across;

        let face = normalize(cross(posed.side, BezierTangent(posed.now, t))) * posed.facing;

        ForwardVSOut surface;
        ProjectVertex(surface, float4(position, 1.0), float4(positionBefore, 1.0));
        surface.normal = BladeNormal(face, posed.side, edge * 2.0 - 1.0, look.normalRounding, posed.ground, posed.groundBlend);
        surface.uv = float2(edge, t);
        surface.uv1 = float2(posed.random, float(draw.look.GetOffset()));
        surface.tangent = float4(posed.side, posed.facing);
        surface.materialOffset = look.materialOffset;
        verts[v] = GrassVSOut(surface, float4(BladeTint(look, posed.color, t), BladeOcclusion(look, t)));
    }

    for (uint p = gtid; p < survivors * trisPerBlade; p += cMeshGroupSize)
    {
        let blade = p / trisPerBlade;
        let local = p % trisPerBlade;
        let base = blade * vertsPerBlade + (local / 2u) * 2u;
        tris[p] = (local & 1u) == 0u ? uint3(base, base + 1u, base + 2u) : uint3(base + 1u, base + 3u, base + 2u);
    }
}
