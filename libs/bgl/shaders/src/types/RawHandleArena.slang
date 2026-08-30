import idl.RawEntry;
import types.RawBuffer;
import types.RawHandleView;

// A raw arena whose records hold resource handles: one allocation, and the two views it takes to
// read it -- raw for the payloads, typed for the handles inside them.
//
// They are one member because they are one allocation. Bound separately they can be handed
// different buffers, and the CPU arena that owns both re-issues the view inside its own growth, so
// there is no instant at which they disagree -- see ADR-9. An arena whose records hold no handle
// stays a plain RawBuffer rather than naming a handle type it has no use for.
public struct RawHandleArena<T>
{
    public RawBuffer raw;
    public RawHandleView<T> handles;

    // Forwarded so a call site reads the arena rather than picking a half of it.
    public P LoadRecordAs<P, U>(RawEntry<U> entry) { return raw.LoadRecordAs<P>(entry); }

    public uint LoadTag<U>(RawEntry<U> entry) { return raw.LoadTag(entry); }

    public T GetHandleAt(uint byteOffset, uint index) { return handles.GetAt(byteOffset, index); }
}
