import debug.dbg;

// Test kernel for the GPU-assertion channel. Thread 7 (and only thread 7) raises a
// single assertion with a known error code, so the CPU-side readback can verify the
// record count, decode and error code. Exercises the implicit gDebug auto-bind: this
// shader declares no other cbuffer.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    if (globalIdx == 7)
    {
        dbg_raise(42u);
    }
}
