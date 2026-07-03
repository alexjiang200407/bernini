import forward.common;
import debug.dbg;

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    dbg_raise(1);
    return float4(1.0, 0.0, 1.0, 1.0);
}
