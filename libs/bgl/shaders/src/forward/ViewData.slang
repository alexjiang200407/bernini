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

    // The animation clock each of the two matrices' frames ran at, in seconds. VAT playback
    // derives its pose from `time` and its motion vector from the pose at `prevTime`.
    public float time;
    public float prevTime;
};
