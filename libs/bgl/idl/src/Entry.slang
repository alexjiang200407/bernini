public struct Entry<T>
{
    // 0 is the null offset, not a live element: every EntryBuffer reserves element 0 for it, so a
    // zero-initialized Entry is null and dereferencing one reads a zeroed element.
    uint offset = 0;

    public uint GetOffset() { return offset; }

    public bool Null() { return offset == 0; }
};
