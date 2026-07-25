public struct DispatchArgs
{
    // Atomically accumulated by the compaction/sort kernels, so its element type carries the atomic
    // (WGSL permits atomic ops only on an atomic<T>). Atomic<uint> is layout-identical to uint, so
    // the indirect-dispatch bytes are unchanged and the C++ mirror stays a plain uint32_t.
    public Atomic<uint> threadCountX;
    public uint threadCountY;
    public uint threadCountZ;
}
