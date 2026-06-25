public interface IRange
{
    public uint GetStart();
    public bool Null();
};

public struct Range<T> : IRange
{
    public uint offsetStart = 0xFFFFFFFF;

    public uint GetStart() { return offsetStart; }

    public bool Null() { return offsetStart == 0xFFFFFFFF; }
};

public struct RangeWithCount<T> : IRange
{
    Range<T> range;
    public uint count;

    public uint GetStart() { return range.GetStart(); }

    public bool Null() { return range.Null(); }
};
