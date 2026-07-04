import idl.Entry;

public struct EntryBuffer<T>
{
    StructuredBuffer<T>.Handle entryBuffer;

    public T Get(Entry<T> entry) { return entryBuffer[entry.GetOffset()]; }

    public T Get(uint index) { return entryBuffer[index]; }

    public __subscript(Entry<T> entry)->T
    {
        get {
            return Get(entry);
        }
    }

    public __subscript(uint index)->T
    {
        get {
            return Get(index);
        }
    }
}
