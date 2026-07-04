import idl.DebugRecord;
import types.ComputeBuffer;
import idl.ErrorCode;

static const uint kDbgHeaderWords = 4;
static const uint kDbgRecordWords = 1;

struct DebugGlobals
{
    ComputeBuffer<uint> buffer;
};

ConstantBuffer<DebugGlobals> gDebug : register(b0, space7);

void dbg_raise(ErrorCode errcode)
{
#if defined(BERNINI_GPU_DEBUG)
    uint slot;
    InterlockedAdd(gDebug.buffer[0], 1u, slot);

    let capacity = gDebug.buffer[2];
    if (slot < capacity)
    {
        let base = kDbgHeaderWords + slot * kDbgRecordWords;
        gDebug.buffer[base + 0] = errcode;
    }
    else
    {
        gDebug.buffer[1] = 1u;
    }
#endif
}

void dbg_assert(bool condition, ErrorCode errcode)
{
    if (!condition)
    {
        dbg_raise(errcode);
    }
}
