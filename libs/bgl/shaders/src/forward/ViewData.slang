public struct ViewData
{
    public float4x4 viewProj;

    // Equal to viewProj on the first frame.
    public float4x4 prevViewProj;

    // The sub-pixel NDC offset each of the two matrices above was built with. Zero unless the target
    // has temporal AA on. ComputeMotionVector subtracts them so velocity describes the surface
    // rather than the sample pattern.
    public float2 jitter;
    public float2 prevJitter;
};
