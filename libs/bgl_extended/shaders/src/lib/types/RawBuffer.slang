import idl.Constants;
import idl.RawEntry;
import idl.RawRange;
import idl.RecordHeader;
import lib.debug.dbg;

// Bytes, not elements: the caller decides the type at each load, which is what a payload whose
// layout varies from one record to the next needs. Everything homogeneous stays in an
// EntryBuffer<T> / RangeBuffer<T>, where the element type is checked once.
//
// A byte offset is a uint on every target, so one of these addresses at most 4 GiB
// (bgl::c_MaxRawBufferBytes); the CPU arena refuses to grow past it.
public struct RawBuffer
{
    ByteAddressBuffer.Handle rawBuffer;

    // @pre byteOffset is 4-byte aligned, and T holds no resource handle -- the element type of a
    // bindless buffer is fixed at its declaration, and a raw one declares bytes, so Metal cannot
    // materialize a texture from what this loads. See docs/slang_shaders.md.
    public T Load<T>(uint byteOffset) { return rawBuffer.Load<T>(byteOffset); }

    // The payload of a record, whose header the entry names. The payload type is named separately
    // from the entry's, because an entry is typed by the interface a record satisfies and a load
    // must name the concrete payload behind it -- as RawEntry<IMaterial> and PbrMaterial::Load do.
    //
    // A null entry reads the arena's reserved head: zeros, but never what the caller meant.
    public P LoadRecordAs<P, T>(RawEntry<T> entry)
    {
        dbg_assert(!entry.Null(), ErrorCode.kNullRawDeref);
        return Load<P>(entry.GetByteOffset() + cRawPayloadOffset);
    }

    // What kind of record sits at an entry, for the one pass that draws more than one kind. A pass
    // compiled for a single kind never asks -- it loads the payload at a fixed offset instead.
    public uint LoadTag<T>(RawEntry<T> entry)
    {
        dbg_assert(!entry.Null(), ErrorCode.kNullRawDeref);
        return Load<RecordHeader>(entry.GetByteOffset()).type;
    }

    // A value inside a headerless range, `byteOffset` past its start.
    public T LoadInRange<T>(RawRange range, uint byteOffset)
    {
        dbg_assert(!range.Null(), ErrorCode.kNullRawDeref, byteOffset);
        return Load<T>(range.GetStart() + byteOffset);
    }
}
