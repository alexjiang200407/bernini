// Where a surface sat last frame, as a UV displacement: history is sampled at `uv - motion`. Both
// clip positions are undivided and already have their frame's jitter removed. NDC y runs opposite to
// UV y, hence the sign flip.
public float2 ComputeMotionVector(float4 clip, float4 prevClip)
{
    float2 ndc = clip.xy / clip.w;
    float2 prevNdc = prevClip.xy / prevClip.w;

    return (ndc - prevNdc) * float2(0.5, -0.5);
}
