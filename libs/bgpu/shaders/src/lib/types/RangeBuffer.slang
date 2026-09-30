import idl.Range;
import lib.debug.dbg;

public struct RangeBuffer<T>
{
    StructuredBuffer<T>.Handle rangeBuffer;

    // A null range reads from the reserved element 0: defined, but never what the caller meant.
    public T Get(IRange entry, uint index)
    {
        dbg_assert(!entry.Null(), ErrorCode.kNullRangeDeref, index);
        return rangeBuffer[entry.GetStart() + index];
    }

    public vector<T, N> Get<let N : uint>(IRange entry, uint index)
    {
        dbg_assert(!entry.Null(), ErrorCode.kNullRangeDeref, index);

        uint baseIndex = entry.GetStart() + index;

        vector<T, N> result;

        [unroll]
        for (uint i = 0; i < N; i++)
        {
            result[i] = rangeBuffer[baseIndex + i];
        }

        return result;
    }

    public __subscript(IRange entry, uint index)->T
    {
        get {
            return Get(entry, index);
        }
    }
}
