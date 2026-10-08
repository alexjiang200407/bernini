import idl.GrassClump;
import idl.GrassLook;
import lib.math.hash;

// A grass blade, built rather than stored: every value comes from its clump and a hash of its index,
// so a blade has no state of its own and a chunk costs its clumps, never its blades.

public static const float cTwoPi = 6.28318530718;

// The inverse of the CPU's EncodeOctahedral (Scene_Grass.cpp): x in the low snorm16, y in the high,
// folded on z.
public float3 DecodeOctahedral(uint packed)
{
    let x = float(int(packed << 16u) >> 16) / 32767.0;
    let y = float(int(packed) >> 16) / 32767.0;
    float3 n = float3(x, y, 1.0 - abs(x) - abs(y));
    if (n.z < 0.0)
    {
        let s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

public float4 UnpackColor(uint packed)
{
    return float4(packed & 0xffu, (packed >> 8u) & 0xffu, (packed >> 16u) & 0xffu, packed >> 24u) / 255.0;
}

// How much of its size every blade of a field keeps at `distance`: all of it up to fadeStart, none
// past fadeEnd, linearly between. The field shrinks as one rather than thinning by count: at a
// runner's speed every blade of a verge crosses whatever threshold a count sets, and each one
// switching -- or even shrinking -- on its own reads as patches of grass swapping in and out.
public float FadeScale(GrassLook look, float distance)
{
    return saturate(1.0 - (distance - look.fadeStart) / (look.fadeEnd - look.fadeStart));
}

// The fewest pixels a blade's height may span and still be drawn. Below it the blade shows nothing,
// so dropping it cannot be seen.
public static const float cGrassMinBladeHeightPixels = 1.0;

// Pixels a blade's height spans per segment before another segment is worth drawing.
public static const float cGrassPixelsPerSegment = 6.0;

// The share of a clump's blades kept at `distance`: every one up to the look's thinStart, then
// (thinStart / distance)^2, so blades per pixel of ground hold, never fewer than one a clump. A
// thinStart of zero keeps every blade.
public float ThinShare(GrassLook look, float distance)
{
    if (look.thinStart <= 0.0 || distance <= look.thinStart)
    {
        return 1.0;
    }
    let ratio = look.thinStart / distance;
    return max(ratio * ratio, 1.0 / float(look.bladesPerClump));
}

// Blades of each clump a chunk launches when its nearest point is `distance` away: the share there
// rounded up, so the blade shrinking out is still drawn. The share only falls with distance, so no
// blade further away keeps more.
public uint KeptBladesPerClump(GrassLook look, float distance)
{
    return min(look.bladesPerClump, uint(ceil(float(look.bladesPerClump) * ThinShare(look, distance))));
}

// How blade `bladeInClump` of a clump is sized at `distance`: `scale` multiplies its height and
// width, and `widen` its width again. Every blade shrinks with the fade, and a fading blade narrows
// more slowly than it shortens (the look's `widening`). Past thinStart a blade the share no longer
// keeps shrinks out over the distance the share falls by one blade, and the survivors widen by the
// inverse of the share, so the field covers as much of the ground with fewer blades.
public struct Thinning
{
    public float scale;
    public float widen;
};

public Thinning ThinningAt(GrassLook look, float distance, uint bladeInClump)
{
    let fade = FadeScale(look, distance);
    let share = ThinShare(look, distance);

    Thinning thinning;
    thinning.scale = fade * saturate(float(look.bladesPerClump) * share - float(bladeInClump));
    thinning.widen = (1.0 + look.widening * (1.0 - fade)) / share;
    return thinning;
}

// Whether a blade `height` tall, already scaled, spans enough of the screen at `distance` to draw.
// `pixelsPerUnit` is what one world unit spans at a distance of one.
public bool BladeVisible(float height, float distance, float pixelsPerUnit)
{
    return height * pixelsPerUnit / max(distance, 1e-3) >= cGrassMinBladeHeightPixels;
}

// Segments along a blade at `distance`: from nearSegments at the camera to farSegments at fadeEnd,
// and no more than the blade's height on screen is worth.
public uint SegmentsAt(GrassLook look, float distance, float bladeHeight, float pixelsPerUnit)
{
    let t = saturate(distance / look.fadeEnd);
    let byDistance = uint(round(lerp(float(look.nearSegments), float(look.farSegments), t)));
    let heightPixels = bladeHeight * pixelsPerUnit / max(distance, 1e-3);
    let byScreen = uint(ceil(heightPixels / cGrassPixelsPerSegment));
    return max(1u, min(byDistance, byScreen));
}

// How far a blade may reach from its root, whatever bends it: a pose keeps the blade's length
// (ADR-13), so its hull stays within the height, with slack for the length correction's estimate
// and half the widest blade.
public float BladeReach(GrassLook look, float heightScale)
{
    // A blade's scale is at most 1, so neither its fade's widening nor its thinning's takes it past
    // this.
    let thinned = look.thinStart > 0.0 ? float(look.bladesPerClump) : 1.0;
    let widest = look.rootWidth * (1.0 + look.widening) * thinned;
    return look.maxHeight * heightScale * 1.25 + 0.5 * widest;
}

// Blades a mesh group holds at `segments`, each 2 * (segments + 1) vertices and 2 * segments
// triangles, within the group's output limits.
public uint BladesPerGroup(uint segments, uint maxVertices, uint maxPrims)
{
    return min(maxVertices / (2u * (segments + 1u)), maxPrims / (2u * segments));
}

// The clump and the blade within it that blade `b` of a chunk of `clumpCount` clumps is. Blades
// interleave across clumps, so a mesh group's run of blades spreads over the chunk's clumps.
public void BladeAddress(uint b, uint clumpCount, out uint clump, out uint bladeInClump)
{
    clump = b % clumpCount;
    bladeInClump = b / clumpCount;
}

// A clump as the blades growing from it read it, whatever made it: a point a mesh stored, or one a
// terrain's grass built where it stands.
public struct ClumpPoint
{
    public float3 position;
    public float heightScale;
    public float3 normal;
    public float3 color;
    public uint seed; // what every blade of the clump hashes its shape from
};

public ClumpPoint StoredClump(GrassClump clump, uint clumpIndex)
{
    ClumpPoint point;
    point.position = clump.position.xyz;
    point.heightScale = clump.position.w;
    point.normal = DecodeOctahedral(clump.normal);
    point.color = UnpackColor(clump.color).rgb;
    point.seed = clumpIndex;
    return point;
}

// A blade at rest, in its mesh's space.
public struct BladeShape
{
    public float3 root;
    public float3 up;     // the clump's ground normal
    public float3 facing; // the way the blade leans, perpendicular to `up`
    public float3 side;   // across the blade
    public float height;
    public float random; // the blade's own [0, 1) value, what a surface reads as variation
};

public BladeShape MakeBladeShape(GrassLook look, ClumpPoint clump, uint bladeInClump)
{
    let seed = HashUint(clump.seed * 16u + bladeInClump + 0x9e3779b9u);
    let r0 = HashFloat(seed);
    let r1 = HashFloat(seed + 1u);
    let r2 = HashFloat(seed + 2u);
    let r3 = HashFloat(seed + 3u);

    BladeShape blade;
    blade.up = clump.normal;

    let helper = abs(blade.up.x) > 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    let tangent = normalize(cross(blade.up, helper));
    let bitangent = cross(blade.up, tangent);

    // Uniform over the clump's disc.
    let radius = sqrt(r0) * look.clumpRadius;
    let offsetAngle = r1 * cTwoPi;
    blade.root = clump.position + (tangent * cos(offsetAngle) + bitangent * sin(offsetAngle)) * radius;

    let facingAngle = r2 * cTwoPi;
    blade.facing = tangent * cos(facingAngle) + bitangent * sin(facingAngle);
    blade.side = cross(blade.up, blade.facing);

    blade.height = lerp(look.minHeight, look.maxHeight, r3) * clump.heightScale;
    blade.random = HashFloat(seed + 4u);
    return blade;
}

// A view's wind, world space: a steady push along `direction` plus a gust field scrolling with it.
public struct GrassWind
{
    public float2 direction; // unit, in the xz plane
    public float strength;
    public float gustScale;
    public float gustSpeed;
    public float gustStrength;
};

float LatticeValue(int2 corner)
{
    return HashFloat(uint(corner.x) * 0x8da6b343u ^ uint(corner.y) * 0xd8163841u);
}

// Smooth value noise in [0, 1] over a lattice of hashed corners.
public float ValueNoise(float2 p)
{
    let cell = floor(p);
    let f = p - cell;
    let u = f * f * (3.0 - 2.0 * f);
    let c = int2(cell);
    let bottom = lerp(LatticeValue(c), LatticeValue(c + int2(1, 0)), u.x);
    let top = lerp(LatticeValue(c + int2(0, 1)), LatticeValue(c + int2(1, 1)), u.x);
    return lerp(bottom, top, u.y);
}

// What the wind pushes a blade's tip by at `time`, as a share of the blade's height along the wind:
// the steady push and the gust under the root, softened by the look's stiffness, with a flutter of
// the blade's own so neighbours do not move as one.
public float WindPush(GrassLook look, GrassWind wind, float3 root, float random, float time)
{
    let scroll = wind.direction * (wind.gustSpeed * time);
    let gust = wind.gustStrength * ValueNoise((root.xz - scroll) / wind.gustScale);
    let push = (wind.strength + look.gustResponse * gust) * (1.0 - look.stiffness);
    let flutter = 0.15 * push * sin(cTwoPi * (time * (1.2 + 0.6 * random) + random));
    return push + flutter;
}

// A blade posed: the quadratic Bezier's three control points. The one place anything bends a blade
// (ADR-13) -- wind and every later force add to `push`, evaluated once at the frame's time and once at
// the previous frame's so a velocity describes them.
//
// The tip leans out by the look's lean plus `push`, never past most of the height, and drops so the
// blade keeps its length; the middle point bows between a straight blade (curvature 0) and a guide
// that stands above the root and sinks as the tip leans (curvature 1). A length correction then
// brings the curve back to the blade's height. See docs/grass.md for where the model comes from.
public struct BladePose
{
    public float3 p0;
    public float3 p1;
    public float3 p2;
};

public BladePose PoseBlade(GrassLook look, BladeShape blade, float3 push)
{
    let h = blade.height;

    var lean = blade.facing * (h * look.lean) + push;
    lean -= blade.up * dot(lean, blade.up);
    let reach = length(lean);
    let maxReach = 0.95 * h;
    if (reach > maxReach)
    {
        lean *= maxReach / reach;
    }
    let out = min(reach, maxReach) / h;

    BladePose pose;
    pose.p0 = blade.root;
    pose.p2 = blade.root + lean + blade.up * (h * sqrt(1.0 - out * out));

    let guide = blade.root + blade.up * (h * max(1.0 - out, 0.05 * max(out, 1.0)));
    pose.p1 = lerp((pose.p0 + pose.p2) * 0.5, guide, look.curvature);

    let chord = distance(pose.p0, pose.p2);
    let polygon = distance(pose.p0, pose.p1) + distance(pose.p1, pose.p2);
    let scale = h / ((2.0 * chord + polygon) / 3.0);
    pose.p1 = pose.p0 + scale * (pose.p1 - pose.p0);
    pose.p2 = pose.p1 + scale * (pose.p2 - pose.p1);
    return pose;
}

public float3 BezierPoint(BladePose pose, float t)
{
    let u = 1.0 - t;
    return u * u * pose.p0 + 2.0 * u * t * pose.p1 + t * t * pose.p2;
}

public float3 BezierTangent(BladePose pose, float t)
{
    return 2.0 * (1.0 - t) * (pose.p1 - pose.p0) + 2.0 * t * (pose.p2 - pose.p1);
}

// How far a blade at `distance` shades with its ground's normal rather than its own: the look's near
// share at the camera, its far share at the fade end.
public float GroundNormalBlend(GrassLook look, float distance)
{
    return lerp(look.groundNormalNear, look.groundNormalFar, saturate(distance / look.fadeEnd));
}

// The shading normal at a vertex of a blade: its face, already turned toward the camera, tilted toward
// the edge the vertex sits on (`edgeSign` -1 or 1 along `side`), then blended toward the ground.
public float3 BladeNormal(float3 face, float3 side, float edgeSign, float rounding, float3 ground, float groundBlend)
{
    let rounded = normalize(face + side * (edgeSign * rounding));
    return normalize(lerp(rounded, ground, groundBlend));
}

// What a blade's base colour is multiplied by along it, from its root (t = 0) to its tip: the look's
// tints over the blade's own colour -- its clump's, darkened by up to the look's variation.
public float3 BladeTint(GrassLook look, float3 bladeColor, float t)
{
    return lerp(look.rootTint.rgb, look.tipTint.rgb, t) * bladeColor;
}

public float3 BladeColor(GrassLook look, ClumpPoint clump, float random)
{
    return clump.color * (1.0 - look.tipTint.w * random);
}

// Occlusion from the root, where the field is densest, to an open tip.
public float BladeOcclusion(GrassLook look, float t)
{
    return lerp(1.0 - look.rootOcclusion, 1.0, t);
}
