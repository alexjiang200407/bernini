// Where a surface sat last frame, as a UV displacement: history is sampled at `uv - motion`. Both
// positions are in NDC with their frame's jitter already removed. NDC y runs opposite to UV y, hence
// the sign flip.
public float2 MotionFromNdc(float2 ndc, float2 prevNdc)
{
    return (ndc - prevNdc) * float2(0.5, -0.5);
}

// The same, from undivided clip positions.
public float2 ComputeMotionVector(float4 clip, float4 prevClip)
{
    float2 ndc = clip.xy / clip.w;
    float2 prevNdc = prevClip.xy / prevClip.w;

    return MotionFromNdc(ndc, prevNdc);
}
