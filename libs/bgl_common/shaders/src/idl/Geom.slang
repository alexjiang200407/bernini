import Constants;
import RangeWithCount;
import Submesh;

// What a geometry-creating method produced and every placement from it shares. A placement names one
// by Entry<Geom> rather than holding a copy, so two placements of one mesh cannot disagree about
// where its submeshes are.
public struct Geom
{
    // Local space, enclosing every submesh of level 0 -- folded from their bounds at upload, not
    // cooked. A placement's size on screen is measured from this once, so all its submeshes land
    // on the same level; each submesh's own sphere would give each its own. xyz centre, w radius.
    public float4 boundingSphere;

    // lodCount equal runs, level-major: submesh s of level n is entry n * SubmeshCount() + s, so
    // a SubmeshInstance's submeshIndex stays the source submesh's whatever level draws it.
    public RangeWithCount<Submesh> submeshes;
    public uint lodCount;

    // Level n is drawn while the placement's projected diameter, in pixels, is at least entry n,
    // walked from 0; below every entry nothing is drawn, so entry lodCount - 1 is the draw-nothing
    // size and 0 there means never dropped. Entries past lodCount are unread.
    public float lodMinPixels[cMaxMeshLods];

    // One level's submeshes: the source's count, whichever level draws.
    public uint SubmeshCount() { return submeshes.count / lodCount; }

    // The entry submesh `submeshIndex` of level `lod` occupies in `submeshes`.
    public uint SubmeshEntry(uint lod, uint submeshIndex) { return lod * SubmeshCount() + submeshIndex; }
};
