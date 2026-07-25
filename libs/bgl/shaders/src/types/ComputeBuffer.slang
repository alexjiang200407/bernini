
#if defined(BGL_WGSL)
typealias ComputeBuffer<T> = RWStructuredBuffer<T>;

typealias AtomicComputeBuffer<T : IAtomicable> = RWStructuredBuffer<Atomic<T> >;
#else
typealias ComputeBuffer<T> = RWStructuredBuffer<T>.Handle;

typealias AtomicComputeBuffer<T : IAtomicable> = RWStructuredBuffer<Atomic<T> >.Handle;
#endif
