public struct PackedBuffer<T>
{
#if defined(BGL_WGSL)
    StructuredBuffer<T> packedBuffer;
#else
    StructuredBuffer<T>.Handle packedBuffer;
#endif

    public T Get(uint index) { return packedBuffer[index]; }

    // A padded dispatch cannot rely on an out-of-range read returning zero: DXIL gives zeroes, but
    // WGSL's bounds robustness clamps to the last element, so the tail threads would re-read it.
    public uint GetCount()
    {
        uint count;
        uint stride;
        packedBuffer.GetDimensions(count, stride);
        return count;
    }

    public __subscript(uint index)->T
    {
        get {
            return packedBuffer[index];
        }
    }
}
