import idl.BlobShadow;
import idl.MeshInstance;
import lib.types.EntryBuffer;
import lib.types.Texture;
import lib.types.UploadBuffer;

// One workgroup draws one blob-shadow decal: a screen-space quad over the caster's footprint whose
// pixel stage reconstructs the static receiver under each pixel from the static depth prepass and
// darkens it by a radial falloff, fading with the caster's height above that surface. Composites
// between the opaque buckets and the depth-sorted transparents, colour only -- a blended decal has
// no single depth to reproject, exactly as programs.forward.Transparent has none.

struct Uniforms
{
    UploadBuffer<BlobShadow> blobBuffer;
    EntryBuffer<MeshInstance> meshBuffer;

    // The static receivers' depth, written by the StaticDepth pass under this frame's viewProj.
    Texture2D.Handle staticDepth;

    float4x4 viewProj;

    // Inverse of viewProj above -- jitter included, so a reconstructed pixel lands exactly where
    // the receiver pass rasterized it.
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

// The corners of the shadow volume: a box, so the two footprint axes and the sweep give 2^3. The
// loop below reads i's bits as those three signs, which is why this is not free to change.
static const uint cVolumeCorners = 8;

struct VSOut
{
    float4 position : SV_Position;
    nointerpolation uint blob : BLOB;
};

// The volume a blob's shadow can land in: the caster's footprint swept from just above the caster
// down to fadeHeight below it.
void VolumeCorners(BlobShadow blob, MeshInstance mesh, out float3 corners[cVolumeCorners])
{
    let n = gBlobShadowData.groundNormal;

    let position = float3(mesh.transform[0].w, mesh.transform[1].w, mesh.transform[2].w) + gBlobShadowData.groundNormal * blob.casterLift;
    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));
    let radius = blob.radius * scale;

    let axis = abs(n.x) < 0.7 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    let tangent = normalize(cross(n, axis));
    let bitangent = cross(n, tangent);

    let top = position + n * (blob.fadeHeight * cTopSlack);
    let bottom = position - n * blob.fadeHeight;

    [unroll]
    for (uint i = 0; i < cVolumeCorners; ++i)
    {
        let sx = (i & 1) != 0 ? 1.0 : -1.0;
        let sy = (i & 2) != 0 ? 1.0 : -1.0;
        corners[i] = ((i & 4) != 0 ? top : bottom) + (tangent * sx + bitangent * sy) * radius;
    }
}

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gid: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 4> verts, OutputIndices<uint3, 2> tris)
{
    let blob = gBlobShadowData.blobBuffer[gid];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];

    SetMeshOutputCounts(4, 2);

    // The quad covers the volume's screen bounds rather than rasterizing the volume itself: a
    // bounding rect needs no winding or camera-inside case, and the pixel stage rejects by
    // reconstruction anyway. A corner at or behind the near plane makes the projected bounds
    // meaningless, so that quad covers the whole viewport instead.
    float3 corners[cVolumeCorners];
    VolumeCorners(blob, mesh, corners);

    var lo = float2(1.0, 1.0);
    var hi = float2(-1.0, -1.0);
    var behind = false;

    [unroll]
    for (uint i = 0; i < cVolumeCorners; ++i)
    {
        let clip = mul(gBlobShadowData.viewProj, float4(corners[i], 1.0));
        if (clip.w <= 0.0)
        {
            behind = true;
        }
        else
        {
            let ndc = clip.xy / clip.w;
            lo = min(lo, ndc);
            hi = max(hi, ndc);
        }
    }

    if (behind)
    {
        lo = float2(-1.0, -1.0);
        hi = float2(1.0, 1.0);
    }

    lo = max(lo, float2(-1.0, -1.0));
    hi = min(hi, float2(1.0, 1.0));

    if (gtid < 4)
    {
        let corner = float2((gtid & 1) != 0 ? hi.x : lo.x, (gtid & 2) != 0 ? hi.y : lo.y);

        VSOut vout;
        vout.position = float4(corner, 0.0, 1.0);
        vout.blob = gid;
        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
        tris[1] = uint3(2, 1, 3);
    }
}

// The static receiver under a raster position: its depth texel unprojected through the same
// jittered matrix the receiver pass rasterized with.
float3 ReceiverWorld(float2 position, float depth)
{
    let rect = gBlobShadowData.viewportRect;
    let ndc = float2((position.x - rect.x) * rect.z * 2.0 - 1.0, 1.0 - (position.y - rect.y) * rect.w * 2.0);
    let world = mul(gBlobShadowData.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

struct PSOut
{
    float4 color : SV_Target;

    // The fragment takes the receiver's own depth, so the kLessOrEqual test against the scene
    // depth occludes the decal wherever a unit stands between the camera and the surface it lands
    // on -- the quad's rasterized depth means nothing.
    float depth : SV_Depth;
};

[shader("pixel")]
PSOut PSMain(VSOut input)
{
    let pixel = uint2(input.position.xy);
    let depth = gBlobShadowData.staticDepth.Load(pixel).r;

    // A cleared texel has no static receiver under it.
    if (depth >= 1.0)
    {
        discard;
    }

    let world = ReceiverWorld(input.position.xy, depth);

    PSOut output;
    output.depth = depth;

    let blob = gBlobShadowData.blobBuffer[input.blob];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];
    let n = gBlobShadowData.groundNormal;

    let caster = float3(mesh.transform[0].w, mesh.transform[1].w, mesh.transform[2].w) + n * blob.casterLift;
    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));

    // A shadow falls down: a surface above its caster receives nothing, one below fades with the
    // per-pixel gap, so a bush top just under the animal is dark while the ground either side of
    // it is faint.
    let delta = caster - world;
    let height = dot(delta, n);
    if (height < 0.0)
    {
        discard;
    }

    let fade = saturate(1.0 - height / blob.fadeHeight);
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
    let zxp = gBlobShadowData.staticDepth.Load(pixel + uint2(1, 0)).r;
    let zxm = gBlobShadowData.staticDepth.Load(pixel - uint2(1, 0)).r;
    let zyp = gBlobShadowData.staticDepth.Load(pixel + uint2(0, 1)).r;
    let zym = gBlobShadowData.staticDepth.Load(pixel - uint2(0, 1)).r;

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
    output.color = float4(0.0, 0.0, 0.0, blob.intensity * fade * falloff * falloff * facing);
    return output;
}
