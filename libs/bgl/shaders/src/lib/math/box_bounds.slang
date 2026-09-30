// The screen rect a box covers, over values alone: its corners and the matrix that projects them.

// A box's corners, ordered so bit k of an index picks the high side of axis k. The 12 edges are
// read off that order, so a box is not free to list its corners any other way.
public static const uint cBoxCorners = 8;

// An NDC rect; `lo` not below `hi` on some axis is a rect that covers nothing.
public struct NdcRect
{
    public float2 lo;
    public float2 hi;

    public bool IsEmpty() { return any(lo >= hi); }
};

// The six clip planes a point lies outside of, one bit each: x and y against +-w, z against the
// [0, w] depth range.
uint ClipOutcode(float4 clip)
{
    uint code = uint(clip.x < -clip.w);
    code |= uint(clip.x > clip.w) << 1;
    code |= uint(clip.y < -clip.w) << 2;
    code |= uint(clip.y > clip.w) << 3;
    code |= uint(clip.z < 0.0) << 4;
    code |= uint(clip.z > clip.w) << 5;
    return code;
}

// Grows the rect to take in a clip-space point, when `take` holds.
void Include(inout NdcRect rect, float4 clip, bool take)
{
    let ndc = clip.xy / clip.w;
    rect.lo = select(bool2(take, take), min(rect.lo, ndc), rect.lo);
    rect.hi = select(bool2(take, take), max(rect.hi, ndc), rect.hi);
}

// The viewport rect the box projects to, after clipping it to the near plane (clip z = 0) and
// clamping to [-1, 1]. Empty when all eight corners lie outside one clip plane. Conservative
// otherwise: a box that only crosses a frustum corner can still come back non-empty.
//
// Selects, not branches: a mesh stage sizes its output from this, and DXC threads a branch that
// decides the count through SetMeshOutputCounts, then rejects the call it duplicated.
public NdcRect ProjectBoxBounds(float4x4 viewProj, float3 corners[cBoxCorners])
{
    float4 clip[cBoxCorners];
    uint outsideAll = 0x3Fu;

    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        clip[i] = mul(viewProj, float4(corners[i], 1.0));
        outsideAll &= ClipOutcode(clip[i]);
    }

    NdcRect rect;
    rect.lo = float2(1.0, 1.0);
    rect.hi = float2(-1.0, -1.0);

    // Behind the near plane a corner's divide is meaningless, so the clipped box's outline there is
    // the points where its edges cross z = 0, and those have w = near > 0.
    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        let inFront = clip[i].z >= 0.0;
        Include(rect, clip[i], inFront);

        [unroll]
        for (uint axis = 0; axis < 3; ++axis)
        {
            if ((i & (1u << axis)) == 0)
            {
                let j = i | (1u << axis);
                let t = clip[i].z / (clip[i].z - clip[j].z);
                Include(rect, lerp(clip[i], clip[j], t), inFront != (clip[j].z >= 0.0));
            }
        }
    }

    let reached = outsideAll == 0;
    rect.lo = select(bool2(reached, reached), max(rect.lo, float2(-1.0, -1.0)), float2(1.0, 1.0));
    rect.hi = select(bool2(reached, reached), min(rect.hi, float2(1.0, 1.0)), float2(-1.0, -1.0));
    return rect;
}
