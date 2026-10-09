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
            return TerrainGrassClump(
                layer,
                terrain,
                grassData.heights,
                grassData.groundCover,
                grassData.heightSampler,
                grassData.groundColorRect,
                tile,
                clumpLocal
            );
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

// A clump's colour as the payload carries it, rgb8.
uint PackClumpColor(float3 color)
{
    let rgb = uint3(round(saturate(color) * 255.0));
    return rgb.r | (rgb.g << 8u) | (rgb.b << 16u) | (255u << 24u);
}

// What a chunk launches its mesh groups with: the blades it keeps, how they are cut, and the chunk's
// clumps that grow anything visible, in their order in the chunk. Blades are numbered across these
// alone (BladeAddress), so a clump that grows nothing launches nothing.
struct GrassPayload
{
    uint chunkRefIndex;
    uint keptBlades;
    uint segments;
    uint bladesPerGroup;
    uint liveCount;

    // The kept clumps, built once for every blade the mesh groups build from them rather than once
    // per blade: position and height scale, ground normal, and colour and seed. Flat arrays of
    // vectors: DXC 1.9 crashes (llvm::cast) on a payload array of structs indexed at run time.
    float4 clumpPositionScale[cGrassClumpsPerChunk];
    float4 clumpNormal[cGrassClumpsPerChunk];
    uint2 clumpColorSeed[cGrassClumpsPerChunk];
};

groupshared GrassPayload gGrassPayload;

ClumpPoint LaunchedClump(float4 positionScale, float4 normal, uint2 colorSeed)
{
    ClumpPoint clump;
    clump.position = positionScale.xyz;
    clump.heightScale = positionScale.w;
    clump.normal = normal.xyz;
    clump.color = UnpackColor(colorSeed.x).rgb;
    clump.seed = colorSeed.y;
    return clump;
}

// Which of the chunk's clumps grow anything visible, a bit each, and what the chunk test found.
groupshared uint gLiveMask[cGrassClumpsPerChunk / 32u];
groupshared uint gChunkVisible;
groupshared float gChunkNearest;
groupshared float gChunkMaxHeightScale;

// One group per chunk reference, one thread per clump. Thread 0 tests the chunk against the frustum
// and its nearest distance; then every thread builds its clump once -- for a terrain, from the
// heightfield and the layer's rules -- and keeps it only if it grows a blade large enough to see
// where it stands. A chunk with no clump kept launches nothing, which is most of a sparse terrain
// layer's window.
[numthreads(cGrassClumpsPerChunk, 1, 1)]
[shader("amplification")]
void ASMain(uint3 gid: SV_GroupID, uint gtid: SV_GroupIndex)
{
    let refIndex = gid.y * grassData.dispatchWidth + gid.x;
    let inRange = refIndex < grassData.refCount;

    ChunkSource source = {};
    var look = GrassLook();
    var found = false;
    if (inRange)
    {
        let chunkRef = grassData.chunkRefs[grassData.firstRef + refIndex];
        let draw = grassData.draws[chunkRef.draw];
        look = grassData.looks.Get(draw.look);
        found = SourceOf(chunkRef, draw, source);
    }

    if (gtid < cGrassClumpsPerChunk / 32u)
    {
        gLiveMask[gtid] = 0u;
    }
    if (gtid == 0u)
    {
        // The nearest any blade of the chunk can be, and the largest any can be scaled.
        var visible = false;
        var nearest = 0.0;
        var maxHeightScale = 1.0;
        if (inRange && found && source.onTerrain)
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
        else if (inRange && found)
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
        if (inRange)
        {
            expansionData.stats[0].grassChunksTested.add(1u);
            if (!visible)
            {
                expansionData.stats[0].grassChunksCulled.add(1u);
            }
        }
#endif

        // A chunk whose largest blade is too small to see where it is nearest launches nothing.
        let tallest = look.maxHeight * maxHeightScale;
        let seen = visible && BladeVisible(tallest * ThinningAt(look, nearest, 0u).scale, nearest, grassData.pixelsPerUnit);
        gChunkVisible = seen ? 1u : 0u;
        gChunkNearest = nearest;
        gChunkMaxHeightScale = maxHeightScale;
    }
    GroupMemoryBarrierWithGroupSync();

    let chunkSeen = gChunkVisible != 0u;
    var live = false;
    var clump = ClumpPoint();
    if (chunkSeen && gtid < source.clumpCount)
    {
        clump = source.Clump(gtid);
        let away = distance(grassData.cameraPos, source.WorldPoint(clump.position));
        let tallest = look.maxHeight * clump.heightScale * ThinningAt(look, away, 0u).scale;
        live = clump.heightScale > 0.0 && BladeVisible(tallest, away, grassData.pixelsPerUnit);
    }
    if (live)
    {
        InterlockedOr(gLiveMask[gtid / 32u], 1u << (gtid % 32u));
    }
    GroupMemoryBarrierWithGroupSync();

    // Kept clumps land in the payload in their order in the chunk: each one's slot is the count of
    // kept clumps below it.
    var liveCount = 0u;
    var below = 0u;
    for (uint word = 0u; word < cGrassClumpsPerChunk / 32u; ++word)
    {
        let bits = gLiveMask[word];
        liveCount += countbits(bits);
        if (word < gtid / 32u)
        {
            below += countbits(bits);
        }
        else if (word == gtid / 32u)
        {
            below += countbits(bits & ((1u << (gtid % 32u)) - 1u));
        }
    }
    if (live)
    {
        gGrassPayload.clumpPositionScale[below] = float4(clump.position, clump.heightScale);
        gGrassPayload.clumpNormal[below] = float4(clump.normal, 0.0);
        gGrassPayload.clumpColorSeed[below] = uint2(PackClumpColor(clump.color), clump.seed);
    }

    // The blades the look keeps at the chunk's nearest point: blades are numbered across the kept
    // clumps (BladeAddress), so the first k of every clump are the first k * clumps.
    let nearest = gChunkNearest;
    let tallest = look.maxHeight * gChunkMaxHeightScale;
    let kept = chunkSeen ? liveCount * KeptBladesPerClump(look, nearest) : 0u;
    let segments = SegmentsAt(look, nearest, tallest, grassData.pixelsPerUnit);
    let perGroup = BladesPerGroup(segments, cGrassVerticesPerGroup, cGrassPrimsPerGroup);
    let groups = (kept + perGroup - 1u) / perGroup;
    if (gtid == 0u)
    {
        gGrassPayload.chunkRefIndex = refIndex;
        gGrassPayload.keptBlades = kept;
        gGrassPayload.segments = segments;
        gGrassPayload.bladesPerGroup = perGroup;
        gGrassPayload.liveCount = liveCount;
#if defined(BERNINI_GPU_DEBUG)
        expansionData.stats[0].grassMeshGroups.add(groups);
#endif
    }
    GroupMemoryBarrierWithGroupSync();

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
    float4 groundColor; // the ground's albedo under the root, and how far the blade takes it
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

    // A blade too small to see where it stands, or thinned out there, is dropped; every other one is
    // drawn, sized by its own distance. The order survivors land in does not matter.
    let b = first + gtid;
    if (b < last)
    {
        uint clumpLocal;
        uint bladeInClump;
        BladeAddress(b, launch.liveCount, clumpLocal, bladeInClump);
        let clump = LaunchedClump(launch.clumpPositionScale[clumpLocal], launch.clumpNormal[clumpLocal], launch.clumpColorSeed[clumpLocal]);
        let rootWorld = source.WorldPoint(clump.position);
        let away = distance(grassData.cameraPos, rootWorld);
        let tallest = look.maxHeight * clump.heightScale * ThinningAt(look, away, bladeInClump).scale;
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
        BladeAddress(gSurvivors[gtid], launch.liveCount, clumpLocal, bladeInClump);
        let clump = LaunchedClump(launch.clumpPositionScale[clumpLocal], launch.clumpNormal[clumpLocal], launch.clumpColorSeed[clumpLocal]);
        var shape = MakeBladeShape(look, clump, bladeInClump);
        let wind = ViewWind();
        let rootWorld = source.WorldPoint(shape.root);
        let distanceToCamera = distance(grassData.cameraPos, rootWorld);
        let thinning = ThinningAt(look, distanceToCamera, bladeInClump);
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
        posed.groundColor = float4(0.0, 0.0, 0.0, 0.0);
        let takesGround = max(look.groundColorNear, look.groundColorFar) > 0.0;
        if (source.onTerrain && takesGround && grassData.groundColorRect.w > 0.0)
        {
            let uv = (rootWorld.xz - grassData.groundColorRect.xy) * grassData.groundColorRect.z;
            let albedo = grassData.groundColor.SampleLevel(grassData.heightSampler, uv, 0.0);
            posed.groundColor = float4(albedo.rgb, GroundColorBlend(look, distanceToCamera) * albedo.a);
        }
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
        verts[v] = GrassVSOut(
            surface, float4(BladeTint(look, posed.color, t), BladeOcclusion(look, t)), posed.groundColor, position.y - posed.now.p0.y
        );
    }

    for (uint p = gtid; p < survivors * trisPerBlade; p += cMeshGroupSize)
    {
        let blade = p / trisPerBlade;
        let local = p % trisPerBlade;
        let base = blade * vertsPerBlade + (local / 2u) * 2u;
        tris[p] = (local & 1u) == 0u ? uint3(base, base + 1u, base + 2u) : uint3(base + 1u, base + 3u, base + 2u);
    }
}
