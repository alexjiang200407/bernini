import idl.DebugRecord;
import types.ComputeBuffer;
import idl.ErrorCode;

__exported import idl.ErrorCode;

static const uint kDbgHeaderWords = 4;
static const uint kDbgRecordWords = 4;

struct DebugGlobals
{
    // Atomic-element: word 0 is bumped atomically to claim a record slot. WGSL then requires every
    // access to the buffer to go through the atomic (no plain read of an atomic<u32>), so the
    // header reads and record writes below use .load/.store -- all layout-identical to a uint.
    AtomicComputeBuffer<uint> buffer;
};

ConstantBuffer<DebugGlobals> gDebug;

void dbg_raise(ErrorCode errcode, uint value = 0, uint limit = 0, uint context = 0)
{
#if defined(BERNINI_GPU_DEBUG)
    let slot = gDebug.buffer[0].add(1u);

    let capacity = gDebug.buffer[2].load();
    if (slot < capacity)
    {
        let base = kDbgHeaderWords + slot * kDbgRecordWords;
        gDebug.buffer[base + 0].store(errcode);
        gDebug.buffer[base + 1].store(value);
        gDebug.buffer[base + 2].store(limit);
        gDebug.buffer[base + 3].store(context);
    }
    else
    {
        gDebug.buffer[1].store(1u);
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
