import lib.forward.common;
import lib.forward.lod_dissolve;

ForwardPSOut Program(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    output.motion = ComputeMotionVector(input);
    return output;
}

[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    return Program(input);
}

// A bucket's dissolve lane: the same surface, dithered against the placement's other level.
[shader("pixel")]
ForwardPSOut PSDissolve(DissolveVSOut input)
{
    DiscardDissolvedLod(input);
    return MarkDissolvedLod(Program(input.Surface()));
}
