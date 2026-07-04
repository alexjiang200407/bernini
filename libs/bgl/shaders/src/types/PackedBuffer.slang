public struct PackedBuffer<T>
{
    StructuredBuffer<T>.Handle packedBuffer;

    public T Get(uint index) { return packedBuffer[index]; }

    public __subscript(uint index)->T
    {
        get {
            return packedBuffer[index];
        }
    }
}
