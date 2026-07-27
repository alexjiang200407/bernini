import idl.DebugRecord;
import types.ComputeBuffer;
import idl.ErrorCode;

__exported import idl.ErrorCode;

static const uint kDbgHeaderWords = 4;
static const uint kDbgRecordWords = 4;

// The debug buffer is an atomic append, so it is read-write -- which a WebGPU vertex stage may not
// bind, and the geometry decode chain runs there. Code shared between the geometry stages gates its
// asserts on this rather than on BERNINI_GPU_DEBUG, so one compiles only where it can run.
#if defined(BERNINI_GPU_DEBUG) && !defined(BGL_WGSL)
#define BGL_DBG_VERTEX_SAFE 1
#endif

struct DebugGlobals
{
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
