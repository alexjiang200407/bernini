import Constants;

/**
 * One view a cull test runs against: the camera today, a shadow cascade later.
 * Planes are normalized (normal, distance) with the positive half-space inside.
 */
public struct CullView
{
    public float4x4 viewProj;
    public float4 frustumPlanes[6];

    // xyz: where the view is, for a placement's true Euclidean distance -- view-space Z would make
    // a pan pop levels at the edge of the frame. w: what one world unit spans on the render grid at
    // a distance of one, in pixels, so a placement's projected diameter is its world radius,
    // doubled, times w over its distance. One float4 because a float3 lays out differently on MSL.
    public float4 cameraPosAndPixelsPerUnit;

    // The view-projection of this view's previous draw, jitter included: what phase 1 of the
    // occlusion cull projects through, since the HZB it reads was built from the depth that matrix
    // rasterized.
    public float4x4 prevViewProj;

    // The draw's viewport in level-0 texels of the HZB (x, y, width, height) and that level's
    // size: the HZB covers the whole depth buffer at half its size, and a view may draw into a part
    // of it. hzbLevelCount is how many levels the ladder holds; a box the last cannot cover reads
    // as visible.
    public float4 hzbRect;
    public float2 hzbLevel0Size;
    public uint hzbLevelCount;

    // The cOcclusion* bits: whether this cull tests occludees at all, and whether the ladder holds
    // the previous draw's depth -- clear on a first draw and after a resize, when phase 1 draws
    // every frustum-visible instance.
    public uint occlusion;

    // ISceneView::SetLodSelection, resolved for this draw: every authored threshold is scaled by
    // lodPixelScale; lodForcedLevel draws that level of every placement (clamped to what it has)
    // -- a LodLevel -- unless it is cLodForceNone, or past every placement's last level when it is
    // cLodForceImpostor; lodFadeStep is how far a level change's dissolve advances this
    // frame, 1 or more being a hard swap.
    public float lodPixelScale;
    public uint lodForcedLevel;
    public float lodFadeStep;

    // The automatic pose source, resolved the same way: posePixels is the threshold on a geom with
    // one level (a geom with levels swaps where it leaves level 0), scaled by lodPixelScale;
    // poseBudget the placements drawn per instance at once; poseForced, unless cPoseForceNone, the
    // source every automatic placement takes, skipping the size test (cPoseForce*).
    public float posePixels;
    public uint poseBudget;
    public uint poseForced;
};

// CullView.occlusion: the view culls its occludees against an HZB (ISceneView::SetOcclusionCulling,
// and only once a draw bucket is an occludee).
public static const uint cOcclusionTestBit = 1;

// CullView.occlusion: the ladder holds the depth of this view's previous draw at this size, so
// phase 1 may test against it.
public static const uint cOcclusionHzbValidBit = 2;
