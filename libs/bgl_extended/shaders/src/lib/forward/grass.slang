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

// How far a blade may reach from its root: the Bezier's hull, so every pose inside it stays within
// the chunk's inflated bound (ADR-13: a force bends a blade about its root and never stretches it).
public float BladeReach(GrassLook look, float heightScale)
{
    return look.maxHeight * heightScale * (1.0 + look.lean + look.curvature);
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

// A blade posed: the quadratic Bezier's three control points. The one place anything bends a blade
// (ADR-13) -- wind and every later force are terms here, evaluated once at the frame's time and once
// at the previous frame's so a velocity describes them.
public struct BladePose
{
    public float3 p0;
    public float3 p1;
    public float3 p2;
};

public BladePose PoseBlade(GrassLook look, BladeShape blade)
{
    BladePose pose;
    pose.p0 = blade.root;
    pose.p1 = blade.root + blade.up * blade.height;
    pose.p2 = blade.root + blade.up * blade.height * (1.0 - look.curvature) + blade.facing * blade.height * (look.lean + look.curvature);
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
