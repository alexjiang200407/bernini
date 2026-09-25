import idl.GrassClump;
import idl.GrassLook;

// A grass blade, built rather than stored: every value comes from its clump and a hash of its index,
// so a blade has no state of its own and a chunk costs its clumps, never its blades.

public static const float cTwoPi = 6.28318530718;

public uint HashUint(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// Uniform in [0, 1).
public float HashFloat(uint x)
{
    return float(HashUint(x) >> 8u) * (1.0 / 16777216.0);
}

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

// The share of a chunk's blades kept at `distance`: all of them up to fadeStart, none past fadeEnd.
public float KeptShare(GrassLook look, float distance)
{
    return saturate(1.0 - (distance - look.fadeStart) / (look.fadeEnd - look.fadeStart));
}

// Segments along a blade at `distance`, from nearSegments at the camera to farSegments at fadeEnd.
public uint SegmentsAt(GrassLook look, float distance)
{
    let t = saturate(distance / look.fadeEnd);
    return uint(round(lerp(float(look.nearSegments), float(look.farSegments), t)));
}

// How far a blade may reach from its root, whatever bends it: a pose keeps the blade's length
// (ADR-13), so its hull stays within the height, with slack for the length correction's estimate
// and half the widest blade.
public float BladeReach(GrassLook look, float heightScale)
{
    return look.maxHeight * heightScale * 1.25 + 0.5 * look.rootWidth * (1.0 + look.widening);
}

// Blades a mesh group holds at `segments`, each 2 * (segments + 1) vertices and 2 * segments
// triangles, within the group's output limits.
public uint BladesPerGroup(uint segments, uint maxVertices, uint maxPrims)
{
    return min(maxVertices / (2u * (segments + 1u)), maxPrims / (2u * segments));
}

// The clump and the blade within it that blade `b` of a chunk of `clumpCount` clumps is. Blades
// interleave across clumps, so keeping the first K of a chunk thins every clump evenly and drops
// one blade at a time as K falls.
public void BladeAddress(uint b, uint clumpCount, out uint clump, out uint bladeInClump)
{
    clump = b % clumpCount;
    bladeInClump = b / clumpCount;
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

public BladeShape MakeBladeShape(GrassLook look, GrassClump clump, uint clumpIndex, uint bladeInClump)
{
    let seed = HashUint(clumpIndex * 16u + bladeInClump + 0x9e3779b9u);
    let r0 = HashFloat(seed);
    let r1 = HashFloat(seed + 1u);
    let r2 = HashFloat(seed + 2u);
    let r3 = HashFloat(seed + 3u);

    BladeShape blade;
    blade.up = DecodeOctahedral(clump.normal);

    let helper = abs(blade.up.x) > 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    let tangent = normalize(cross(blade.up, helper));
    let bitangent = cross(blade.up, tangent);

    // Uniform over the clump's disc.
    let radius = sqrt(r0) * look.clumpRadius;
    let offsetAngle = r1 * cTwoPi;
    blade.root = clump.position.xyz + (tangent * cos(offsetAngle) + bitangent * sin(offsetAngle)) * radius;

    let facingAngle = r2 * cTwoPi;
    blade.facing = tangent * cos(facingAngle) + bitangent * sin(facingAngle);
    blade.side = cross(blade.up, blade.facing);

    blade.height = lerp(look.minHeight, look.maxHeight, r3) * clump.position.w;
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
