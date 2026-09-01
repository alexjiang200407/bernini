// THIS IS A FILE GENERATED FROM RangeWithCount.slang. DO NOT EDIT MANUALLY

import Range;

public struct RangeWithCount<T> : IRange
{
    Range<T> range;
    public uint count;

    public uint GetStart() { return range.GetStart(); }

    public bool Null() { return range.Null(); }
};
