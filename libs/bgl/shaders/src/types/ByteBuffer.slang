public struct ByteBuffer
{
#if defined(BGL_WGSL)
    StructuredBuffer<uint> rangeBuffer;
#else
    StructuredBuffer<uint>.Handle rangeBuffer;
#endif

    public uint LoadWordAtByte(uint byteOffset) { return rangeBuffer[byteOffset >> 2]; }
}
