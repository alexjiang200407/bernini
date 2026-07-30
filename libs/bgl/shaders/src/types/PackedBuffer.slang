public struct PackedBuffer<T>
{
    StructuredBuffer<T>.Handle packedBuffer;

    public T Get(uint index) { return packedBuffer[index]; }

    // Read from the buffer itself, so a padded dispatch can bound its tail without a count
    // plumbed in from the CPU.
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
