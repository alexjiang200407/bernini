import forward.common;

[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    output.motion = ComputeMotionVector(input);
    return output;
}
