// THIS IS A FILE GENERATED FROM Entry.slang. DO NOT EDIT MANUALLY

public struct Entry<T>
{
    // 0 is the null offset, not a live element: every EntryBuffer reserves element 0 for it, so a
    // zero-initialized Entry is null and dereferencing one reads a zeroed element.
    uint offset = 0;

    public uint GetOffset() { return offset; }

    public bool Null() { return offset == 0; }

    // For a shader that computes an element's position rather than being handed one. A field stays
    // non-public so nothing writes one in place: an entry is produced by the buffer, or by a struct
    // that already holds one.
    public static Entry<T> At(uint offset)
    {
        Entry<T> entry;
        entry.offset = offset;
        return entry;
    }
};
