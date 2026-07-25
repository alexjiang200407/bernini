
typealias ComputeBuffer<T> = RWStructuredBuffer<T>.Handle;

// A ComputeBuffer whose elements are updated atomically. WGSL permits atomic ops only on an
// atomic<T> location, so the element type carries the atomic; Atomic<T> is layout-identical to T,
// so the bytes match a plain ComputeBuffer<T> and the DXIL path lowers to the same InterlockedAdd.
// Index it and act through the element -- buf[i].add(1u), .load(), .store(v) -- not InterlockedAdd.
typealias AtomicComputeBuffer<T : IAtomicable> = RWStructuredBuffer<Atomic<T> >.Handle;
