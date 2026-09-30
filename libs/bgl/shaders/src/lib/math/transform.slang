// Affine transform maths, over values alone: a matrix is passed in and a matrix or a vector comes
// back. An affine transform is three rows and an implied (0,0,0,1), which is how a placement and a
// palette slot both store one, so nothing here reads or writes the fourth.

// The inverse of an affine transform: the upper 3x3 inverted through its adjugate, and the
// translation carried back through that.
public float4x4 AffineInverse(float4x4 m)
{
    float3x3 r = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    float3 t = float3(m[0].w, m[1].w, m[2].w);

    float3 c0 = cross(r[1], r[2]);
    float3 c1 = cross(r[2], r[0]);
    float3 c2 = cross(r[0], r[1]);

    float3x3 inv = transpose(float3x3(c0, c1, c2)) / dot(r[0], c0);
    float3 it = -mul(inv, t);

    return float4x4(float4(inv[0], it.x), float4(inv[1], it.y), float4(inv[2], it.z), float4(0.0, 0.0, 0.0, 1.0));
}

public float3 TransformPoint(float4x4 m, float3 p)
{
    return mul(m, float4(p, 1.0)).xyz;
}

public float3 TransformDir(float4x4 m, float3 v)
{
    return mul(m, float4(v, 0.0)).xyz;
}

// A rotation of `angle` about `axis` through `pivot`, as one affine matrix. Rodrigues rather than a
// quaternion: the result is multiplied into a model transform either way, so building the matrix
// directly saves the conversion.
public float4x4 RotationAbout(float3 pivot, float3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    float t = 1.0 - c;

    float3 r0 = float3(t * axis.x * axis.x + c, t * axis.x * axis.y - s * axis.z, t * axis.x * axis.z + s * axis.y);
    float3 r1 = float3(t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + c, t * axis.y * axis.z - s * axis.x);
    float3 r2 = float3(t * axis.x * axis.z - s * axis.y, t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + c);

    // pivot - R * pivot, so the pivot is the one point the rotation leaves alone.
    float3 offset = pivot - float3(dot(r0, pivot), dot(r1, pivot), dot(r2, pivot));

    return float4x4(float4(r0, offset.x), float4(r1, offset.y), float4(r2, offset.z), float4(0.0, 0.0, 0.0, 1.0));
}
