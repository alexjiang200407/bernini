import types.HandleElement;

// The second view of a raw arena: the same allocation, read as handles of T.
//
// A resource type has to be on the binding -- a raw view declares bytes, and Metal will not make a
// resource of them (see docs/slang_shaders.md). So a payload keeps a handle's bytes inline and this
// is what makes a resource of them, one view per handle type the arena stores.
//
// Deliberately not an EntryBuffer. Nothing here is an allocated element, there is no reserved null
// slot, and most offsets are not a T at all -- what makes one a T is the payload layout rule, which
// the arena's record types own. Addressing is therefore in the arena's own coordinates, a byte
// offset, rather than an element index.
public struct RawHandleView<T>
{
    // ScalarDataLayout: tight, C-compatible packing, so an element is the bytes the payload stored.
    // The default structured layout 16-byte-aligns nested handle structs, which a payload does not.
    // Boxed because a buffer of bare handles is not declarable on Metal -- see HandleElement.
    StructuredBuffer<HandleElement<T>, ScalarDataLayout>.Handle handleBuffer;

    // Handle `index` of the payload at `byteOffset`.
    //
    // @pre byteOffset is a whole number of T. The record grid guarantees it, and the arena's owner
    //      is where that is asserted -- a fractional offset truncates onto a neighbouring handle
    //      rather than failing.
    public T GetAt(uint byteOffset, uint index) { return handleBuffer[byteOffset / (uint)sizeof(T) + index].handle; }
}
