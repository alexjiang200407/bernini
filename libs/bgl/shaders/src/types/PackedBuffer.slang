public struct PackedBuffer<T>
{
#if defined(BGL_WGSL)
    StructuredBuffer<T> packedBuffer;
#else
    StructuredBuffer<T>.Handle packedBuffer;
#endif

    public T Get(uint index) { return packedBuffer[index]; }

    public __subscript(uint index)->T
    {
        get {
            return packedBuffer[index];
        }
    }
}
