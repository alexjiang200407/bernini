public struct CullStats
{
    // Atomically accumulated by the cull kernel, so the element type carries the atomic WGSL
    // requires. Atomic<uint> is layout-identical to uint, so the C++ mirror stays a plain uint32_t.
    public Atomic<uint> tested;
    public Atomic<uint> frustumCulled;
}
