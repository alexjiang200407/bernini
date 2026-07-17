import debug.dbg;
import idl.ErrorCode;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    if (globalIdx == 7)
    {
        // The payload is what makes a record worth reading, so it has to survive the round trip --
        // distinct values, so a decode that transposes or drops one cannot pass.
        dbg_raise(ErrorCode.kUnknown, 111, 222, 333);
    }
}
