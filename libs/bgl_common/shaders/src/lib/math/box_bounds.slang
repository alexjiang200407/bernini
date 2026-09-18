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
    uint code = 0;
    code |= clip.x < -clip.w ? 1u : 0u;
    code |= clip.x > clip.w ? 2u : 0u;
    code |= clip.y < -clip.w ? 4u : 0u;
    code |= clip.y > clip.w ? 8u : 0u;
    code |= clip.z < 0.0 ? 16u : 0u;
    code |= clip.z > clip.w ? 32u : 0u;
    return code;
}

void Include(inout NdcRect rect, float4 clip)
{
    let ndc = clip.xy / clip.w;
    rect.lo = min(rect.lo, ndc);
    rect.hi = max(rect.hi, ndc);
}

// The viewport rect the box projects to, after clipping it to the near plane (clip z = 0) and
// clamping to [-1, 1]. Empty when all eight corners lie outside one clip plane. Conservative
// otherwise: a box that only crosses a frustum corner can still come back non-empty.
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

    if (outsideAll != 0)
    {
        return rect;
    }

    // Behind the near plane a corner's divide is meaningless, so the clipped box's outline there is
    // the points where its edges cross z = 0, and those have w = near > 0.
    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        let inFront = clip[i].z >= 0.0;
        if (inFront)
        {
            Include(rect, clip[i]);
        }

        [unroll]
        for (uint axis = 0; axis < 3; ++axis)
        {
            let j = i | (1u << axis);
            if (j != i && inFront != (clip[j].z >= 0.0))
            {
                let t = clip[i].z / (clip[i].z - clip[j].z);
                Include(rect, lerp(clip[i], clip[j], t));
            }
        }
    }

    rect.lo = max(rect.lo, float2(-1.0, -1.0));
    rect.hi = min(rect.hi, float2(1.0, 1.0));
    return rect;
}
