import types.ComputeBuffer;
__exported import idl.ErrorCode;

static const uint kDbgHeaderWords = 4;
static const uint kDbgRecordWords = 4;

// Narrower than BERNINI_GPU_DEBUG: the buffer below is an atomic append, so it is read-write, and a
// WebGPU vertex stage may not bind one. Code that a vertex stage can reach gates its asserts on
// this, so an assert compiles only where it could actually run.
#if defined(BERNINI_GPU_DEBUG) && !defined(BGL_WGSL)
#define BERNINI_GPU_DEBUG_ALL_STAGES 1
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
