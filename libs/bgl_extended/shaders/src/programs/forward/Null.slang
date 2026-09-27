import lib.forward.common;
import lib.forward.lod_dissolve;

[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    DiscardDissolvedLod(input);

    ForwardPSOut output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    output.motion = ComputeMotionVector(input);
    return MarkDissolvedLod(output, input);
}
