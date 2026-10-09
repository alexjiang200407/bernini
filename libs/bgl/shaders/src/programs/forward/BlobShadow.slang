import idl.BlobShadow;
import idl.Clip;
import idl.Constants;
import idl.DominantFrames;
import idl.InstanceLod;
import idl.InstancePose;
import idl.MeshInstance;
import idl.MeshInstanceFlag;
import idl.PlaybackType;
import idl.Rig;
import idl.SkinnedAutoState;
import idl.SkinnedState;
import idl.SkinnedTableState;
import lib.anim.clip_playback;
import lib.anim.skinning;
import lib.math.box_bounds;
import lib.math.transform;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One workgroup draws one blob-shadow decal: a screen-space quad over the caster's footprint whose
// pixel stage reconstructs the receiver under each pixel from the depth as Forward's world phase
// left it -- the world alone, no unit in it -- and darkens it by a radial falloff, fading with the
// caster's height above that surface. Composites before the skinned phase, so a unit drawn after
// covers it, and before the transparents, which composite over it; colour only -- a blended decal
// has no single depth to reproject, exactly as programs.forward.Transparent has none.
//
// An entry is either a placement's own disc, cast from its origin, or one foot's: a capsule lying on
// the sole from heel to ball, cast from the pose the frame draws. The two differ only in where the
// shadow is cast from and how high it is measured; the falloff and the facing are one path.

struct Uniforms
{
    UploadBuffer<BlobShadow> blobBuffer;
    EntryBuffer<MeshInstance> meshBuffer;

    // The palette arena, GPU-written: read only for the soles PoseSkinned wrote at the end of a
    // hero's slice this frame.
    ComputeBuffer<float4> palettes;

    // A foot's placement's playback record and its rig: where its sole sits in the arena.
    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;

    // A table placement's soles: its rig's table arena, the clip it plays, and the clock that picks
    // the frame.
    ComputeBuffer<float4> boneAnimTables;
    RangeBuffer<Clip> clipBuffer;
    float time;

    // Discs listed: the groups past them, in a dispatch folded over two dimensions, emit nothing.
    uint blobCount;

    // An automatic placement's table pose: the frames its table soles are read at.
    ComputeBuffer<DominantFrames> dominantFrames;

    // An automatic placement's source this frame, and its pose-pool slice when it is drawn per
    // instance: the camera's cull wrote both.
    ComputeBuffer<InstanceLod> instanceLod;
    ComputeBuffer<InstancePose> instancePose;

    // The scene depth after Forward's world phase, under this frame's viewProj: every receiver,
    // and nothing that is not one.
    Texture2D.Handle worldDepth;

    // Each grass pixel's height above its blade's root, zero where no blade drew
    // (lib.forward.grass_vertex): a blade takes the shadow as the ground it grows from would.
    Texture2D.Handle grassRootHeight;

    float4x4 viewProj;

    // Inverse of viewProj above -- jitter included, so a reconstructed pixel lands exactly where
    // the world phase rasterized it.
    float4x4 invViewProj;

    // The scene's up axis, unit length: the direction a shadow is cast along.
    float3 groundNormal;

    // This draw's viewport as (minX, minY, 1/width, 1/height): SV_Position arrives in
    // target-absolute pixels and the reconstruction needs viewport-relative NDC.
    float4 viewportRect;
};

ConstantBuffer<Uniforms> gBlobShadowData;

// How far the disc shrinks by the time it has fully faded: a receiver far below its caster sees a
// disc tightened toward half the grounded radius as the contact softens away.
static const float cLiftShrink = 0.5;

// Groups a dispatch takes along x: a mesh dispatch's limit per dimension on every backend here.
static const uint cBlobDispatchWidth = 65535u;

// The quad's slack above the caster origin, as a share of fadeHeight: a receiver exactly at the
// caster's height sits inside the covered volume instead of on its boundary.
static const float cTopSlack = 0.02;

// The receiver facing (its normal against up) at which the shadow reaches full strength: below,
// it ramps out, so a surface tilted past ~70 degrees -- a wall -- receives nothing.
static const float cFacingRamp = 0.35;

// A foot's shadow on grass, against the same foot's on bare ground: wider, since a paw sinks among
// the blades around it, and stronger, since blades break a contact shadow up. The disc is not
// widened: it already spans the blades a placement stands in.
static const float cGrassFootWiden = 3.0;
static const float cGrassFootStrength = 1.5;

struct VSOut
{
    float4 position : SV_Position;
    nointerpolation uint blob : BLOB;

    // A foot's sole, world space, read once per group rather than per pixel. Unread on a body disc.
    nointerpolation float3 heelPos : HEEL;
    nointerpolation float3 ballPos : BALL;
};

// The volume a blob's shadow can land in: the caster's footprint swept from just above the caster
// down to fadeHeight below it.
void VolumeCorners(BlobShadow blob, MeshInstance mesh, out float3 corners[cBoxCorners])
{
    let n = gBlobShadowData.groundNormal;

    let position = float3(mesh.transform[0].w, mesh.transform[1].w, mesh.transform[2].w) + gBlobShadowData.groundNormal * blob.lift;
    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));
    let radius = blob.radius * scale;

    let axis = abs(n.x) < 0.7 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    let tangent = normalize(cross(n, axis));
    let bitangent = cross(n, tangent);

    let top = position + n * (blob.fadeHeight * cTopSlack);
    let bottom = position - n * blob.fadeHeight;

    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        let sx = (i & 1) != 0 ? 1.0 : -1.0;
        let sy = (i & 2) != 0 ? 1.0 : -1.0;
        corners[i] = ((i & 4) != 0 ? top : bottom) + (tangent * sx + bitangent * sy) * radius;
    }
}

// The volume a foot's shadow can land in: the sole's heel-to-ball segment widened by the radius,
// swept from `lift` above the higher end down to fadeHeight below the lower one. A box in the ground
// plane's own axes, so the corners keep the order VolumeCorners gives them.
void FootVolumeCorners(BlobShadow blob, MeshInstance mesh, float3 heelPos, float3 ballPos, out float3 corners[cBoxCorners])
{
    let n = gBlobShadowData.groundNormal;

    // As wide as the shadow reaches on grass: a bare-ground pixel past the bare radius falls off to
    // nothing in the pixel stage.
    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));
    let radius = blob.radius * scale * cGrassFootWiden;

    let axis = abs(n.x) < 0.7 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    let tangent = normalize(cross(n, axis));
    let bitangent = cross(n, tangent);

    let heelIn = float3(dot(heelPos, tangent), dot(heelPos, bitangent), dot(heelPos, n));
    let ballIn = float3(dot(ballPos, tangent), dot(ballPos, bitangent), dot(ballPos, n));
    let lo = min(heelIn, ballIn) - float3(radius, radius, blob.fadeHeight);
    let hi = max(heelIn, ballIn) + float3(radius, radius, blob.lift + blob.fadeHeight * cTopSlack);

    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        let x = (i & 1) != 0 ? hi.x : lo.x;
        let y = (i & 2) != 0 ? hi.y : lo.y;
        let z = (i & 4) != 0 ? hi.z : lo.z;
        corners[i] = tangent * x + bitangent * y + n * z;
    }
}

// A heel and ball from two table sole slots (TableSoleSlot), blended as the skin is and placed by
// the placement's transform: model space and unplanted, since the table has no IK.
void PlaceTableSole(MeshInstance mesh, uint lower, uint upper, float upperWeight, out float3 heel, out float3 ball)
{
    let tables = gBlobShadowData.boneAnimTables;
    let model = mesh.Matrix();
    heel = TransformPoint(model, lerp(tables[lower].xyz, tables[upper].xyz, upperWeight));
    ball = TransformPoint(model, lerp(tables[lower + 1].xyz, tables[upper + 1].xyz, upperWeight));
}

// Where `leg`'s heel and ball stand this frame, world space, from whichever pose the placement draws.
// A hero palette ends with one sole per leg of its rig, after the poses at `time` and `prevTime`,
// placed and planted by the pose pass. A table placement reads its rig's table soles at the frame its
// clip has reached -- model space and unplanted, the two frames it falls between blended as the
// skin is -- and places them by its own transform.
//
// @pre mesh's rig authored `leg`.
void TableSole(MeshInstance mesh, Rig rig, Clip clip, float frames, uint leg, out float3 heel, out float3 ball)
{
    uint lower;
    uint upper;
    float weight;
    TableSoleSlots(rig, clip, frames, leg, lower, upper, weight);
    PlaceTableSole(mesh, lower, upper, weight, heel, ball);
}

// The palette slots of `leg`'s heel and ball in a pose slice from `base`: after the poses at
// `time` and `prevTime`, one sole per leg, placed and planted by the pose pass.
void PaletteSole(Rig rig, uint base, uint leg, out float3 heel, out float3 ball)
{
    let slot = base + cFloat4sPerBone * rig.boneCount * 2u + cFloat4sPerSole * leg;
    heel = gBlobShadowData.palettes[slot].xyz;
    ball = gBlobShadowData.palettes[slot + 1].xyz;
}

void FootSole(uint placement, MeshInstance mesh, uint leg, out float3 heel, out float3 ball)
{
    let kind = PlaybackType(gBlobShadowData.playbackBuffer.LoadTag(mesh.playback));
    if (kind == PlaybackType.kSkinnedTable)
    {
        let state = gBlobShadowData.playbackBuffer.LoadRecordAs<SkinnedTableState>(mesh.playback);
        let rig = gBlobShadowData.rigs.Get(state.playback.rig);
        let clip = gBlobShadowData.clipBuffer.Get(rig.clips, state.playback.clip);
        TableSole(mesh, rig, clip, ClipFrames(clip, state.playback, mesh.PlaybackTime(gBlobShadowData.time)), leg, heel, ball);
        return;
    }

    if (kind == PlaybackType.kSkinnedAuto)
    {
        let state = gBlobShadowData.playbackBuffer.LoadRecordAs<SkinnedAutoState>(mesh.playback);
        let rig = gBlobShadowData.rigs.Get(state.rig);

        // The entry the dissolve is heading toward casts: the feet a unit is about to stand on.
        let word = gBlobShadowData.instanceLod[placement];
        if (!word.Unset() && !word.FromTable())
        {
            PaletteSole(rig, gBlobShadowData.instancePose[placement].palette.GetStart(), leg, heel, ball);
            return;
        }

        let dominant = gBlobShadowData.dominantFrames[placement];
        PlaceTableSole(
            mesh,
            TableSoleSlot(rig, dominant.lowerFrame, leg),
            TableSoleSlot(rig, dominant.upperFrame, leg),
            dominant.upperWeight,
            heel,
            ball
        );
        return;
    }

    let state = gBlobShadowData.playbackBuffer.LoadRecordAs<SkinnedState>(mesh.playback);
    PaletteSole(gBlobShadowData.rigs.Get(state.rig), state.palette.GetStart(), leg, heel, ball);
}

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint3 groupId: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 4> verts, OutputIndices<uint3, 2> tris)
{
    // A dispatch's x counts to cBlobDispatchWidth and y carries the rest, so a list past one
    // dimension's limit still draws every disc; the last row's groups past the list emit nothing,
    // reading the first disc so no index runs off the buffer.
    let gid = groupId.x + groupId.y * cBlobDispatchWidth;
    let listed = gid < gBlobShadowData.blobCount;
    let blob = gBlobShadowData.blobBuffer[listed ? gid : 0u];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];

    // A branch, not a select: a body disc's `leg` is no index, and a select loads both sides.
    var heelPos = float3(0.0, 0.0, 0.0);
    var ballPos = float3(0.0, 0.0, 0.0);
    float3 corners[cBoxCorners];
    if (blob.leg != cBodyDisc)
    {
        FootSole(blob.mesh, mesh, blob.leg, heelPos, ballPos);
        FootVolumeCorners(blob, mesh, heelPos, ballPos, corners);
    }
    else
    {
        VolumeCorners(blob, mesh, corners);
    }

    // The quad covers the volume's screen bounds rather than rasterizing the volume itself: a
    // bounding rect needs no winding or camera-inside case, and the pixel stage rejects by
    // reconstruction anyway. A volume the frustum does not reach emits nothing, since every pixel
    // it covered would be rejected.
    // A block's slot its writer hid this frame is still listed, since the CPU never learns which
    // it hid: it emits nothing.
    let rect = ProjectBoxBounds(gBlobShadowData.viewProj, corners);
    let quads = uint(listed && !rect.IsEmpty() && !HasMeshInstanceFlag(mesh, MeshInstanceFlag.kHidden));
    SetMeshOutputCounts(4 * quads, 2 * quads);

    if (gtid < 4 * quads)
    {
        let corner = float2((gtid & 1) != 0 ? rect.hi.x : rect.lo.x, (gtid & 2) != 0 ? rect.hi.y : rect.lo.y);

        VSOut vout;
        vout.position = float4(corner, 0.0, 1.0);
        vout.blob = gid;
        vout.heelPos = heelPos;
        vout.ballPos = ballPos;
        verts[gtid] = vout;
    }

    if (gtid < quads)
    {
        tris[0] = uint3(0, 1, 2);
        tris[1] = uint3(2, 1, 3);
    }
}

// The receiver under a raster position: its depth texel unprojected through the same jittered
// matrix the world phase rasterized with.
float3 ReceiverWorld(float2 position, float depth)
{
    let rect = gBlobShadowData.viewportRect;
    let ndc = float2((position.x - rect.x) * rect.z * 2.0 - 1.0, 1.0 - (position.y - rect.y) * rect.w * 2.0);
    let world = mul(gBlobShadowData.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// The point of the heel-to-ball segment nearest `receiver` across the ground -- measured in the plane
// normal to `n` -- so a heel raised off a planted toe fades while the toe stays dark.
float3 NearestOnSole(float3 heelPos, float3 ballPos, float3 receiver, float3 n)
{
    let segment = ballPos - heelPos;
    let across = segment - n * dot(segment, n);
    let length2 = dot(across, across);
    let t = length2 > 0.0 ? saturate(dot(receiver - heelPos, across) / length2) : 0.0;
    return heelPos + segment * t;
}

// A shadow also lands only on what faces up: without this a wall beside the caster catches a
// vertical smear, and the ramp keeps walkable slopes at full strength. The facing cannot come
// from the raster quad's own derivatives, though: a quad spanning a silhouette -- a grass
// blade against the street behind it -- differences two different surfaces, and the cross of
// those vectors is the gap between them, not a normal; under the sub-pixel jitter those edge
// quads land differently every frame, so a decal draped over foliage flickered along every
// blade. Per axis, the neighbour nearer in depth is the one on this pixel's own surface, and
// the normal is built one-sided from those two. A neighbour off the viewport reads as a
// cleared texel, which the selection then avoids like any other far side of an edge.
float GroundFacing(float2 position, uint2 pixel, float depth, float3 world, float3 n)
{
    let zxp = gBlobShadowData.worldDepth.Load(int3(pixel + uint2(1, 0), 0)).r;
    let zxm = gBlobShadowData.worldDepth.Load(int3(pixel - uint2(1, 0), 0)).r;
    let zyp = gBlobShadowData.worldDepth.Load(int3(pixel + uint2(0, 1), 0)).r;
    let zym = gBlobShadowData.worldDepth.Load(int3(pixel - uint2(0, 1), 0)).r;

    let xPlus = abs(zxp - depth) <= abs(zxm - depth);
    let yPlus = abs(zyp - depth) <= abs(zym - depth);
    let sideX = ReceiverWorld(position + float2(xPlus ? 1.0 : -1.0, 0.0), xPlus ? zxp : zxm);
    let sideY = ReceiverWorld(position + float2(0.0, yPlus ? 1.0 : -1.0), yPlus ? zyp : zym);

    // Signed so the one-sided differences keep the two-sided winding whichever side was nearer.
    let dx = xPlus ? sideX - world : world - sideX;
    let dy = yPlus ? sideY - world : world - sideY;

    let normal = normalize(cross(dy, dx));
    return smoothstep(0.0, cFacingRamp, dot(normal, n));
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let pixel = uint2(input.position.xy);
    let depth = gBlobShadowData.worldDepth.Load(int3(pixel, 0)).r;

    // A cleared texel has no receiver under it.
    if (depth >= 1.0)
    {
        discard;
    }

    let n = gBlobShadowData.groundNormal;

    // A blade pixel receives as its ground: lowered to its root's height, and facing up. The height
    // is world Y, as the grass stage measured it, whatever the ground normal.
    let rootHeight = gBlobShadowData.grassRootHeight.Load(int3(pixel, 0)).r;
    let onGrass = rootHeight > 0.0;
    let world = ReceiverWorld(input.position.xy, depth) - float3(0.0, rootHeight, 0.0);

    let blob = gBlobShadowData.blobBuffer[input.blob];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];

    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));

    // A shadow falls down: a surface above its caster receives nothing, one below fades with the
    // per-pixel gap, so a bush top just under the animal is dark while the ground either side of
    // it is faint.
    float3 delta;
    float height;
    float fade;
    if (blob.leg == cBodyDisc)
    {
        let caster = float3(mesh.transform[0].w, mesh.transform[1].w, mesh.transform[2].w) + n * blob.lift;
        delta = caster - world;
        height = dot(delta, n);
        if (height < 0.0)
        {
            discard;
        }

        fade = saturate(1.0 - height / blob.fadeHeight);
    }
    else
    {
        // The lift only lets a receiver sit that far above the sole: a planted sole is at street
        // level, and measuring the fade from a lifted point would pre-fade exactly the foot that
        // should be darkest.
        delta = NearestOnSole(input.heelPos, input.ballPos, world, n) - world;
        height = dot(delta, n);
        if (height + blob.lift < 0.0)
        {
            discard;
        }

        fade = saturate(1.0 - max(height, 0.0) / blob.fadeHeight);
    }

    if (fade <= 0.0)
    {
        discard;
    }

    var facing = 1.0;
    if (!onGrass)
    {
        facing = GroundFacing(input.position.xy, pixel, depth, world, n);
        if (facing <= 0.0)
        {
            discard;
        }
    }

    let grassFoot = onGrass && blob.leg != cBodyDisc;
    let radius = blob.radius * scale * lerp(cLiftShrink, 1.0, fade) * (grassFoot ? cGrassFootWiden : 1.0);
    let intensity = grassFoot ? saturate(blob.intensity * cGrassFootStrength) : blob.intensity;
    let lateral = delta - n * height;
    let falloff = saturate(1.0 - dot(lateral, lateral) / (radius * radius));

    // Premultiplied black: the One/InvSrcAlpha blend scales the scene by 1 - alpha, so the decal
    // only darkens. Squared for a soft shoulder at the rim.
    return float4(0.0, 0.0, 0.0, intensity * fade * falloff * falloff * facing);
}
