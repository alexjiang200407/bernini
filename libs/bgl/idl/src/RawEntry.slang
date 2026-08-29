public struct RawEntry<T>
{
    // A byte offset into a raw arena, naming the record's header; its payload begins
    // cRawPayloadOffset bytes later. 0 is null, not a live record: every arena reserves a zeroed
    // record there, so a zero-initialized RawEntry is null and dereferencing one reads zeros.
    uint byteOffset = 0;

    public uint GetByteOffset() { return byteOffset; }

    public bool Null() { return byteOffset == 0; }

    // For a shader handed a bare offset. A field stays non-public so nothing writes one in place:
    // an entry is produced by the arena, or by a struct that already holds one.
    public static RawEntry<T> At(uint byteOffset)
    {
        RawEntry<T> entry;
        entry.byteOffset = byteOffset;
        return entry;
    }
};
