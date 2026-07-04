import forward.common;
import debug.dbg;
import idl.ErrorCode;

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    // Unconditionally raise a marker code to exercise the pixel-stage assert path.
    dbg_raise(ErrorCode.kUnknown);
    return float4(1.0, 0.0, 1.0, 1.0);
}
