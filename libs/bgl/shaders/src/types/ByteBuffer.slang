public struct ByteBuffer
{
    StructuredBuffer<uint>.Handle rangeBuffer;

    public uint LoadWord(uint wordIndex) { return rangeBuffer[wordIndex]; }

    public uint LoadWordAtByte(uint byteOffset) { return rangeBuffer[byteOffset >> 2]; }
}
