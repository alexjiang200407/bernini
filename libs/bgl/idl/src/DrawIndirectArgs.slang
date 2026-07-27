// A WebGPU non-indexed drawIndirect record. Distinct from DispatchArgs, which is 12 bytes of thread
// group counts: these four fields are the layout drawIndirect reads, and vertexCount counts vertices.
//
// vertexCount is the expansion kernel's allocator as well as its output -- a thread reserves its
// records by adding their vertices to it, so no separate count and no finalize pass.
public struct DrawIndirectArgs
{
    public Atomic<uint> vertexCount;
    public uint instanceCount;
    public uint firstVertex;
    public uint firstInstance;
}
