// THIS IS A FILE GENERATED FROM Entry.slang. DO NOT EDIT MANUALLY

public struct Entry<T>
{
    uint offset = 0xFFFFFFFF;

    public uint GetOffset() { return offset; }

    public bool Null() { return offset == 0xFFFFFFFF; }
};
