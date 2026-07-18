import idl.Entry;

public struct EntryBuffer<T>
{
    // ScalarDataLayout: tight, C-compatible packing so element T matches the CPU mirror bgl_idlgen
    // emits (it reflects this same layout; idl structs are memcpy'd straight in). The default
    // structured-buffer layout 16-byte-aligns nested handle structs, which the mirror does not.
    StructuredBuffer<T, ScalarDataLayout>.Handle entryBuffer;

    public T Get(Entry<T> entry) { return entryBuffer[entry.GetOffset()]; }

    public T Get(uint index) { return entryBuffer[index]; }

    public __subscript(Entry<T> entry)->T
    {
        get {
            return Get(entry);
        }
    }

    public __subscript(uint index)->T
    {
        get {
            return Get(index);
        }
    }
}
