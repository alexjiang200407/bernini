// The read-only view of a CPU-authored bgl::UploadBuffer<T>. ScalarDataLayout: the CPU side is
// memcpy'd in, so element T must match its mirror the way EntryBuffer's does.
public typealias UploadBuffer<T> = StructuredBuffer<T, ScalarDataLayout>.Handle;
