
typealias ComputeBuffer<T> = RWStructuredBuffer<T>.Handle;

typealias AtomicComputeBuffer<T : IAtomicable> = RWStructuredBuffer<Atomic<T> >.Handle;

// The writable counterpart of types.RawBuffer, for a pass that produces bytes rather than elements.
typealias RawComputeBuffer = RWByteAddressBuffer.Handle;
