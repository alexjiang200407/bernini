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

    // This dispatch's run of the chunk references, laid out dispatchWidth groups to a row.
    uint firstRef;
    uint refCount;
    uint dispatchWidth;
};

ConstantBuffer<GrassData> grassData;

struct GrassPayload
{
    uint ref;
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
        let ref = grassData.chunkRefs[grassData.firstRef + refIndex];
        let draw = grassData.draws[ref.draw];
        let look = grassData.looks.Get(draw.look);
        let chunk = grassData.chunks[ref.chunk];
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
            let kept = min(total, uint(ceil(float(total) * KeptShare(look, nearest))));
            let segments = max(1u, SegmentsAt(look, nearest));
            let perGroup = BladesPerGroup(segments, cGrassVerticesPerGroup, cGrassPrimsPerGroup);

            gGrassPayload.ref = refIndex;
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

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload GrassPayload payload,
    OutputVertices<ForwardVSOut, cGrassVerticesPerGroup> verts,
    OutputIndices<uint3, cGrassPrimsPerGroup> tris
)
{
    let ref = grassData.chunkRefs[grassData.firstRef + payload.ref];
    let draw = grassData.draws[ref.draw];
    let look = grassData.looks.Get(draw.look);
    let chunk = grassData.chunks[ref.chunk];
    let mesh = forwardData.meshBuffer.Get(draw.mesh);
    let total = chunk.clumpCount * look.bladesPerClump;

    let first = gid * payload.bladesPerGroup;
    let last = min(payload.keptBlades, first + payload.bladesPerGroup);

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
        let keep = KeptShare(look, distance(grassData.cameraPos, rootWorld));
        if (float(b) < float(total) * keep)
        {
            uint slot;
            InterlockedAdd(gSurvivorCount, 1u, slot);
            gSurvivors[slot] = b;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    let survivors = gSurvivorCount;
    let segments = payload.segments;
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

    for (uint v = gtid; v < survivors * vertsPerBlade; v += cMeshGroupSize)
    {
        let blade = gSurvivors[v / vertsPerBlade];
        let row = (v % vertsPerBlade) / 2u;
        let edge = float((v % vertsPerBlade) & 1u);

        uint clumpLocal;
        uint bladeInClump;
        BladeAddress(blade, chunk.clumpCount, clumpLocal, bladeInClump);
        let clumpIndex = chunk.firstClump + clumpLocal;
        let clump = grassData.clumps[clumpIndex];
        let shape = MakeBladeShape(look, clump, clumpIndex, bladeInClump);
        let pose = PoseBlade(look, shape);

        let rootWorld = mesh.TransformPoint(shape.root);
        let keep = KeptShare(look, distance(grassData.cameraPos, rootWorld));
        let widen = 1.0 + look.widening * (1.0 - keep);

        let t = float(row) / float(segments);
        let width = look.rootWidth * lerp(1.0, look.tipWidth, t) * widen;
        let centre = BezierPoint(pose, t);
        let position = centre + shape.side * (edge - 0.5) * width;
        let normal = normalize(cross(shape.side, BezierTangent(pose, t)));

        ForwardVSOut vertex;
        ProjectVertex(vertex, float4(mesh.TransformPoint(position), 1.0), float4(mesh.PrevTransformPoint(position), 1.0));
        vertex.normal = normalize(mesh.TransformDirection(normal));
        vertex.uv = float2(edge, t);
        vertex.uv1 = cNoUv1;
        vertex.tangent = float4(normalize(mesh.TransformDirection(shape.side)), 1.0);
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
