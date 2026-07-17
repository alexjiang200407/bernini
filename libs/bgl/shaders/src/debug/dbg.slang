import idl.DebugRecord;
import types.ComputeBuffer;
import idl.ErrorCode;

__exported import idl.ErrorCode;

static const uint kDbgHeaderWords = 4;
static const uint kDbgRecordWords = 4;

struct DebugGlobals
{
    ComputeBuffer<uint> buffer;
};

ConstantBuffer<DebugGlobals> gDebug;

void dbg_raise(ErrorCode errcode, uint value = 0, uint limit = 0, uint context = 0)
{
#if defined(BERNINI_GPU_DEBUG)
    uint slot;
    InterlockedAdd(gDebug.buffer[0], 1u, slot);

    let capacity = gDebug.buffer[2];
    if (slot < capacity)
    {
        let base = kDbgHeaderWords + slot * kDbgRecordWords;
        gDebug.buffer[base + 0] = errcode;
        gDebug.buffer[base + 1] = value;
        gDebug.buffer[base + 2] = limit;
        gDebug.buffer[base + 3] = context;
    }
    else
    {
        gDebug.buffer[1] = 1u;
    }
#endif
}

void dbg_assert(bool condition, ErrorCode errcode, uint value = 0, uint limit = 0, uint context = 0)
{
    if (!condition)
    {
        dbg_raise(errcode, value, limit, context);
    }
}
