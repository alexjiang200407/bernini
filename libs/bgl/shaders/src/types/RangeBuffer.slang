import idl.Range;

public struct RangeBuffer<T>
{
#if defined(BGL_WGSL)
    StructuredBuffer<T> rangeBuffer;
#else
    StructuredBuffer<T>.Handle rangeBuffer;
#endif

    public T Get(IRange entry, uint index) { return rangeBuffer[entry.GetStart() + index]; }

    public vector<T, N> Get<let N : uint>(IRange entry, uint index)
    {
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
