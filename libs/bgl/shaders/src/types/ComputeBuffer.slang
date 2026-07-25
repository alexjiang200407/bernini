
typealias ComputeBuffer<T> = RWStructuredBuffer<T>.Handle;

typealias AtomicComputeBuffer<T : IAtomicable> = RWStructuredBuffer<Atomic<T> >.Handle;
