// Bytes, not elements: the caller decides the type at each load, which is what a payload whose
// layout varies from one record to the next needs. Everything homogeneous stays in an
// EntryBuffer<T> / RangeBuffer<T>, where the element type is checked once.
//
// A byte offset is a uint on every target, so one of these addresses at most 4 GiB
// (bgl::c_MaxRawBufferBytes); the CPU arena refuses to grow past it.
public struct RawBuffer
{
    ByteAddressBuffer.Handle rawBuffer;

    // @pre byteOffset is 4-byte aligned. Slang lowers this to what the target can do at that
    // alignment; an unaligned offset reads neighbouring bytes rather than faulting.
    public T Load<T>(uint byteOffset) { return rawBuffer.Load<T>(byteOffset); }
}
