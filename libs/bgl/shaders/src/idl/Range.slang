// THIS IS A FILE GENERATED FROM Range.slang. DO NOT EDIT MANUALLY

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
