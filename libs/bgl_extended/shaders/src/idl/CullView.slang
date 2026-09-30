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

    // ISceneView::SetLodSelection, resolved for this draw: every authored threshold is scaled by
    // lodPixelScale; lodForcedLevel draws that level of every placement (clamped to what it has)
    // -- a LodLevel -- unless it is cLodForceNone; lodFadeStep is how far a level change's dissolve advances this
    // frame, 1 or more being a hard swap.
    public float lodPixelScale;
    public uint lodForcedLevel;
    public float lodFadeStep;
};
