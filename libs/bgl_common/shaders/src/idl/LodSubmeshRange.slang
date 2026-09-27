import Range;
import Submesh;

// A geom's submeshes, every level of detail of them: lodCount equal runs of submeshCount entries,
// level-major, so submesh s of level n is entry n * submeshCount + s. A SubmeshInstance's
// submeshIndex is the source submesh's, whichever level draws it, and only Entry turns it and a
// level into a place in the range -- index the range any other way and it reads another level's
// geometry.
public struct LodSubmeshRange : IRange
{
    public Range<Submesh> range;

    // One level's: the source's submeshes, and so the SubmeshInstances one placement holds.
    public uint submeshCount;
    public uint lodCount;

    public uint GetStart() { return range.GetStart(); }

    public bool Null() { return range.Null(); }

    public uint SubmeshCount() { return submeshCount; }

    public uint LodCount() { return lodCount; }

    // Every entry the range holds, all levels.
    public uint TotalCount() { return submeshCount * lodCount; }

    // @pre lod < lodCount, submeshIndex < submeshCount.
    public uint Entry(uint lod, uint submeshIndex) { return lod * submeshCount + submeshIndex; }
};
