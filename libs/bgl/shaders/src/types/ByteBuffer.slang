public struct ByteBuffer
{
    StructuredBuffer<uint>.Handle rangeBuffer;

    public uint LoadWordAtByte(uint byteOffset) { return rangeBuffer[byteOffset >> 2]; }
}
