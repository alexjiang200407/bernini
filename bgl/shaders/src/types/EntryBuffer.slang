import db.Entry;

public struct EntryBuffer<T> {
    StructuredBuffer<T>.Handle entryBuffer;

    public T Get(Entry<T> entry) {
        return entryBuffer[entry.GetOffset()];
    }

    public T Get(uint index) {
        return entryBuffer[index];
    }
}