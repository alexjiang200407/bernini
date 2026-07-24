import forward.common;
import debug.dbg;
import idl.ErrorCode;

[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    // Unconditionally raise a marker code to exercise the pixel-stage assert path.
    dbg_raise(ErrorCode.kUnknown);

    ForwardPSOut output;
    output.color = float4(1.0, 0.0, 1.0, 1.0);
    output.motion = ComputeMotionVector(input);
    return output;
}
