/**
 * One view a cull test runs against: the camera today, a shadow cascade later.
 * Planes are normalized (normal, distance) with the positive half-space inside.
 */
public struct CullView
{
    public float4x4 viewProj;
    public float4 frustumPlanes[6];
};
