import idl.BlobShadow;
import idl.Constants;
import idl.MeshInstance;
import lib.math.box_bounds;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.Texture;
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

    // The scene depth after Forward's world phase, under this frame's viewProj: every receiver,
    // and nothing that is not one.
    Texture2D.Handle worldDepth;

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

// The quad's slack above the caster origin, as a share of fadeHeight: a receiver exactly at the
// caster's height sits inside the covered volume instead of on its boundary.
static const float cTopSlack = 0.02;

// The receiver facing (its normal against up) at which the shadow reaches full strength: below,
// it ramps out, so a surface tilted past ~70 degrees -- a wall -- receives nothing.
static const float cFacingRamp = 0.35;

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

    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));
    let radius = blob.radius * scale;

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

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gid: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 4> verts, OutputIndices<uint3, 2> tris)
{
    let blob = gBlobShadowData.blobBuffer[gid];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];

    // A branch, not a select: a body disc's `soleSlot` is no index, and a select loads both sides.
    var heelPos = float3(0.0, 0.0, 0.0);
    var ballPos = float3(0.0, 0.0, 0.0);
    float3 corners[cBoxCorners];
    if (blob.soleSlot != cBodyDisc)
    {
        heelPos = gBlobShadowData.palettes[blob.soleSlot].xyz;
        ballPos = gBlobShadowData.palettes[blob.soleSlot + 1].xyz;
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
    let rect = ProjectBoxBounds(gBlobShadowData.viewProj, corners);
    let quads = uint(!rect.IsEmpty());
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

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let pixel = uint2(input.position.xy);
    let depth = gBlobShadowData.worldDepth.Load(pixel).r;

    // A cleared texel has no receiver under it.
    if (depth >= 1.0)
    {
        discard;
    }

    let world = ReceiverWorld(input.position.xy, depth);

    let blob = gBlobShadowData.blobBuffer[input.blob];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];
    let n = gBlobShadowData.groundNormal;

    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));

    // A shadow falls down: a surface above its caster receives nothing, one below fades with the
    // per-pixel gap, so a bush top just under the animal is dark while the ground either side of
    // it is faint.
    float3 delta;
    float height;
    float fade;
    if (blob.soleSlot == cBodyDisc)
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

    // A shadow also lands only on what faces up: without this a wall beside the caster catches a
    // vertical smear, and the ramp keeps walkable slopes at full strength. The facing cannot come
    // from the raster quad's own derivatives, though: a quad spanning a silhouette -- a grass
    // blade against the street behind it -- differences two different surfaces, and the cross of
    // those vectors is the gap between them, not a normal; under the sub-pixel jitter those edge
    // quads land differently every frame, so a decal draped over foliage flickered along every
    // blade. Per axis, the neighbour nearer in depth is the one on this pixel's own surface, and
    // the normal is built one-sided from those two. A neighbour off the viewport reads as a
    // cleared texel, which the selection then avoids like any other far side of an edge.
    let zxp = gBlobShadowData.worldDepth.Load(pixel + uint2(1, 0)).r;
    let zxm = gBlobShadowData.worldDepth.Load(pixel - uint2(1, 0)).r;
    let zyp = gBlobShadowData.worldDepth.Load(pixel + uint2(0, 1)).r;
    let zym = gBlobShadowData.worldDepth.Load(pixel - uint2(0, 1)).r;

    let xPlus = abs(zxp - depth) <= abs(zxm - depth);
    let yPlus = abs(zyp - depth) <= abs(zym - depth);
    let sideX = ReceiverWorld(input.position.xy + float2(xPlus ? 1.0 : -1.0, 0.0), xPlus ? zxp : zxm);
    let sideY = ReceiverWorld(input.position.xy + float2(0.0, yPlus ? 1.0 : -1.0), yPlus ? zyp : zym);

    // Signed so the one-sided differences keep the two-sided winding whichever side was nearer.
    let dx = xPlus ? sideX - world : world - sideX;
    let dy = yPlus ? sideY - world : world - sideY;

    let normal = normalize(cross(dy, dx));
    let facing = smoothstep(0.0, cFacingRamp, dot(normal, n));
    if (facing <= 0.0)
    {
        discard;
    }

    let radius = blob.radius * scale * lerp(cLiftShrink, 1.0, fade);
    let lateral = delta - n * height;
    let falloff = saturate(1.0 - dot(lateral, lateral) / (radius * radius));

    // Premultiplied black: the One/InvSrcAlpha blend scales the scene by 1 - alpha, so the decal
    // only darkens. Squared for a soft shoulder at the rim.
    return float4(0.0, 0.0, 0.0, blob.intensity * fade * falloff * falloff * facing);
}
