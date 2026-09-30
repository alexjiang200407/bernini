public interface IRange
{
    public uint GetStart();
    public bool Null();
};

public struct Range<T> : IRange
{
    // 0 is the null offset, not a live element: every RangeBuffer reserves element 0 for it, so a
    // zero-initialized Range is null and dereferencing one reads a zeroed element.
    public uint offsetStart = 0;

    public uint GetStart() { return offsetStart; }

    public bool Null() { return offsetStart == 0; }
};
