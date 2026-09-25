import idl.Constants;
import idl.GrassChunk;
import idl.GrassChunkRef;
import idl.GrassClump;
import idl.GrassDraw;
import idl.GrassLook;
import idl.MeshInstance;
import lib.forward.common;
import lib.forward.ExpansionData;
import lib.forward.grass;
import lib.forward.mesh_stage;
import lib.geom.vertexdecode;
import lib.math.frustum;
import lib.types.EntryBuffer;
import lib.types.UploadBuffer;

// The grass geometry stage: one amplification group per chunk of a view's grass list, culled to the
// frustum and thinned by distance, launching mesh groups that each build a run of the chunk's blades.
// Everything after ForwardVSOut is an ordinary material draw -- the pixel program is the one the
// material's own bucket uses.

struct GrassData
{
    UploadBuffer<GrassDraw> draws;
    UploadBuffer<GrassChunkRef> chunkRefs;
    EntryBuffer<GrassLook> looks;

    // Scene ranges, but addressed by the global indices the chunk references and chunks store.
    StructuredBuffer<GrassChunk, ScalarDataLayout>.Handle chunks;
    StructuredBuffer<GrassClump, ScalarDataLayout>.Handle clumps;

    float3 cameraPos;

    // What one world unit spans on the render grid, in pixels, at a distance of one.
    float pixelsPerUnit;

    // The view's wind (GrassWind): the direction's xz, unit.
    float2 windDirection;
    float windStrength;
    float windGustScale;
    float windGustSpeed;
    float windGustStrength;

    // This dispatch's run of the chunk references, laid out dispatchWidth groups to a row.
    uint firstRef;
    uint refCount;
    uint dispatchWidth;
};

ConstantBuffer<GrassData> grassData;

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

    if (refIndex < grassData.refCount)
    {
        let chunkRef = grassData.chunkRefs[grassData.firstRef + refIndex];
        let draw = grassData.draws[chunkRef.draw];
        let look = grassData.looks.Get(draw.look);
        let chunk = grassData.chunks[chunkRef.chunk];
        let mesh = forwardData.meshBuffer.Get(draw.mesh);

        let local = float4(chunk.boundingSphere.xyz, chunk.boundingSphere.w + BladeReach(look, chunk.maxHeightScale) + look.clumpRadius);
        let world = mesh.TransformSphere(local);
        let visible = SphereInFrustum(expansionData.cullView[0].frustumPlanes, world.xyz, world.w);

#if defined(BERNINI_GPU_DEBUG)
        expansionData.stats[0].grassChunksTested.add(1u);
        if (!visible)
        {
            expansionData.stats[0].grassChunksCulled.add(1u);
        }
#endif

        if (visible)
        {
            // The nearest any blade of the chunk can be: the most it can keep.
            let nearest = max(0.0, distance(grassData.cameraPos, world.xyz) - world.w);
            let total = chunk.clumpCount * look.bladesPerClump;
            let thinning = ThinningAt(look, nearest, grassData.pixelsPerUnit);
            let kept = min(total, uint(ceil(float(total) * thinning.kept)));
            let tallest = look.maxHeight * chunk.maxHeightScale;
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
    float width; // the root's, widened as the field thins
};
groupshared PosedBlade gPosed[cGrassVerticesPerGroup / 4u];

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload GrassPayload launch,
    OutputVertices<ForwardVSOut, cGrassVerticesPerGroup> verts,
    OutputIndices<uint3, cGrassPrimsPerGroup> tris
)
{
    let chunkRef = grassData.chunkRefs[grassData.firstRef + launch.chunkRefIndex];
    let draw = grassData.draws[chunkRef.draw];
    let look = grassData.looks.Get(draw.look);
    let chunk = grassData.chunks[chunkRef.chunk];
    let mesh = forwardData.meshBuffer.Get(draw.mesh);
    let total = chunk.clumpCount * look.bladesPerClump;

    let first = gid * launch.bladesPerGroup;
    let last = min(launch.keptBlades, first + launch.bladesPerGroup);

    if (gtid == 0u)
    {
        gSurvivorCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // Each blade kept or dropped against its own distance, so the field thins smoothly across a
    // chunk rather than in steps between chunks. The order survivors land in does not matter.
    let b = first + gtid;
    if (b < last)
    {
        uint clumpLocal;
        uint bladeInClump;
        BladeAddress(b, chunk.clumpCount, clumpLocal, bladeInClump);
        let clump = grassData.clumps[chunk.firstClump + clumpLocal];
        let rootWorld = mesh.TransformPoint(clump.position.xyz);
        let thinning = ThinningAt(look, distance(grassData.cameraPos, rootWorld), grassData.pixelsPerUnit);
        if (float(b) < float(total) * thinning.kept)
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
        BladeAddress(gSurvivors[gtid], chunk.clumpCount, clumpLocal, bladeInClump);
        let clumpIndex = chunk.firstClump + clumpLocal;
        let shape = MakeBladeShape(look, grassData.clumps[clumpIndex], clumpIndex, bladeInClump);
        let wind = ViewWind();
        let placed = PlacedShape(mesh, shape, false);
        let placedBefore = PlacedShape(mesh, shape, true);
        let thinning = ThinningAt(look, distance(grassData.cameraPos, placed.root), grassData.pixelsPerUnit);

        PosedBlade posed;
        posed.now = PoseAt(look, wind, placed, viewData.time);
        posed.before = PoseAt(look, wind, placedBefore, viewData.prevTime);
        posed.side = placed.side;
        posed.sideBefore = placedBefore.side;
        posed.width = look.rootWidth * thinning.widen;
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

        ForwardVSOut vertex;
        ProjectVertex(vertex, float4(position, 1.0), float4(positionBefore, 1.0));
        vertex.normal = normalize(cross(posed.side, BezierTangent(posed.now, t)));
        vertex.uv = float2(edge, t);
        vertex.uv1 = cNoUv1;
        vertex.tangent = float4(posed.side, 1.0);
        vertex.materialOffset = look.materialOffset;
        verts[v] = vertex;
    }

    for (uint p = gtid; p < survivors * trisPerBlade; p += cMeshGroupSize)
    {
        let blade = p / trisPerBlade;
        let local = p % trisPerBlade;
        let base = blade * vertsPerBlade + (local / 2u) * 2u;
        tris[p] = (local & 1u) == 0u ? uint3(base, base + 1u, base + 2u) : uint3(base + 1u, base + 3u, base + 2u);
    }
}
