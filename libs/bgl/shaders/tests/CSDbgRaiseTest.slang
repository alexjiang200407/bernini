import debug.dbg;
import idl.ErrorCode;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    if (globalIdx == 7)
    {
        dbg_raise(ErrorCode.kUnknown);
    }
}
