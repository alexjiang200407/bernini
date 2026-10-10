import idl.CullView;

// The occlusion test of the instance cull, over values alone: where in the HZB ladder a bounding
// sphere's screen box is read, and what the four texels read there say. The program that owns the
// level handles does the gather (programs.culling.CullInstances, CullOccluded).
//
// The box is the sphere's world-space bounding box projected corner by corner: it contains the
// sphere, which contains the mesh, so a box the HZB hides is one no pixel of the mesh reaches. A
// corner at or behind the near plane makes the sphere visible outright.

public struct HzbQuery
{
    // Settled without the ladder: the sphere crosses the near plane, or its box spans more than
    // the last level covers.
    public bool visible;

    // The level whose texels the box spans at most one of, and where in it to gather: a 2x2
    // around this point covers the box.
    public uint level;
    public float2 uv;

    // The box's nearest clip depth -- the largest, under reversed-Z.
    public float nearest;
};

// The level a box of `extent` level-0 texels spans at most one texel of.
uint LevelFor(float extent)
{
    return extent <= 1.0 ? 0u : uint(ceil(log2(extent)));
}

// A level's size: each level is half the one above, rounded up (HzbChain).
uint2 LevelSize(uint2 level0, uint level)
{
    uint2 size = level0;
    for (uint i = 0; i < level; ++i)
    {
        size = (size + uint2(1u, 1u)) / 2u;
    }
    return size;
}

public HzbQuery PrepareHzbQuery(CullView view, float4x4 viewProj, float4 sphere)
{
    HzbQuery query;
    query.visible = true;
    query.level = 0u;
    query.uv = float2(0.0, 0.0);
    query.nearest = 0.0;

    float2 lo = float2(1.0, 1.0);
    float2 hi = float2(-1.0, -1.0);
    float nearest = 0.0;

    [unroll]
    for (uint i = 0; i < 8u; ++i)
    {
        let corner = sphere.xyz + sphere.w * float3((i & 1u) != 0u ? 1.0 : -1.0, (i & 2u) != 0u ? 1.0 : -1.0, (i & 4u) != 0u ? 1.0 : -1.0);
        let clip = mul(viewProj, float4(corner, 1.0));

        // Reversed-Z: the near plane is z = w. A corner on or past it has no useful divide.
        if (clip.w <= 0.0 || clip.z >= clip.w)
        {
            return query;
        }

        let ndc = clip.xyz / clip.w;
        lo = min(lo, ndc.xy);
        hi = max(hi, ndc.xy);
        nearest = max(nearest, ndc.z);
    }

    // Off the viewport the ladder holds nothing to hide behind: the box is clamped to it, and a
    // box wholly outside was the frustum test's to reject.
    lo = clamp(lo, float2(-1.0, -1.0), float2(1.0, 1.0));
    hi = clamp(hi, float2(-1.0, -1.0), float2(1.0, 1.0));

    // NDC y is up and the texture's y is down.
    let uvLo = float2(lo.x * 0.5 + 0.5, 0.5 - hi.y * 0.5);
    let uvHi = float2(hi.x * 0.5 + 0.5, 0.5 - lo.y * 0.5);
    let texelLo = view.hzbRect.xy + uvLo * view.hzbRect.zw;
    let texelHi = view.hzbRect.xy + uvHi * view.hzbRect.zw;
    let extent = texelHi - texelLo;

    let level = LevelFor(max(extent.x, extent.y));
    if (level >= view.hzbLevelCount)
    {
        return query;
    }

    // Level-0 texel t sits under texel floor(t / 2^level) of `level`, whatever the rounded sizes
    // along the way, so the box's centre is scaled by the power of two and normalised by the
    // level's own size.
    let scale = exp2(-float(level));
    let centre = 0.5 * (texelLo + texelHi) * scale;
    let size = LevelSize(uint2(view.hzbLevel0Size), level);

    query.visible = false;
    query.level = level;
    query.uv = centre / float2(size);
    query.nearest = nearest;
    return query;
}

// Whether the four texels gathered at the query hide the box: its nearest point is farther than
// the farthest of them. Equality keeps the box: a surface the ladder holds at exactly the box's
// depth is the box's own.
public bool HzbHides(float4 gathered, float nearest)
{
    let farthest = min(min(gathered.x, gathered.y), min(gathered.z, gathered.w));
    return nearest < farthest;
}
