// THIS IS A FILE GENERATED FROM RawRange.slang. DO NOT EDIT MANUALLY

// A window of bytes in a raw arena, carrying no header: nothing about the bytes says what they
// are, and the owner already knows. Vertex data is the case -- a submesh's VertexLayout is the
// decode rule, stored once per submesh rather than once per vertex.
public struct RawRange
{
    // 0 is null, as in RawEntry, and for the same reason.
    uint byteStart = 0;

    public uint GetStart() { return byteStart; }

    public bool Null() { return byteStart == 0; }

    public static RawRange At(uint byteStart)
    {
        RawRange range;
        range.byteStart = byteStart;
        return range;
    }
};
