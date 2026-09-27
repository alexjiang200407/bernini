import Constants;
import RangeWithCount;
import Submesh;

// What a geometry-creating method produced and every placement from it shares. A placement names one
// by Entry<Geom> rather than holding a copy, so two placements of one mesh cannot disagree about
// where its submeshes are.
public struct Geom
{
    // lodCount runs of submeshCount entries, level-major: submesh s of level n is entry
    // n * submeshCount + s, so a SubmeshInstance's submeshIndex stays the source submesh's whatever
    // level draws it.
    public RangeWithCount<Submesh> submeshes;
    public uint submeshCount;
    public uint lodCount;

    // Local space, over every submesh of level 0: what a placement's size on screen is measured
    // by, once for all its submeshes. xyz is the centre, w the radius.
    public float4 boundingSphere;

    // Level n is drawn while the placement's projected diameter, in pixels, is at least entry n,
    // walked from 0; below every entry nothing is drawn, so entry lodCount - 1 is the draw-nothing
    // size and 0 there means never dropped. Entries past lodCount are unread.
    public float lodMinPixels[cMaxMeshLods];

    // The entry submesh `submeshIndex` of level `lod` occupies in `submeshes`.
    public uint SubmeshEntry(uint lod, uint submeshIndex) { return lod * submeshCount + submeshIndex; }
};
