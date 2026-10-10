import Impostor;
import LodLevel;
import LodSubmeshRange;
import RawEntry;

// What a geometry-creating method produced and every placement from it shares. A placement names one
// by Entry<Geom> rather than holding a copy, so two placements of one mesh cannot disagree about
// where its submeshes are.
public struct Geom
{
    // Local space, enclosing every submesh of level 0 -- folded from their bounds at upload, not
    // cooked. A placement's size on screen is measured from this once, so all its submeshes land
    // on the same level; each submesh's own sphere would give each its own. xyz centre, w radius.
    public float4 boundingSphere;

    public LodSubmeshRange submeshes;

    // Level n is drawn while the placement's projected diameter, in pixels, is at least entry n,
    // walked from 0; below every entry nothing is drawn, so entry submeshes.lodCount - 1 is the
    // draw-nothing size and 0 there means never dropped. Entries past the level count are unread.
    public float lodMinPixels[cMaxMeshLods];

    // Drawn past the last level, below that level's floor; null where the mesh has none, so a
    // placement there draws nothing as before. A record in the impostor arena, whose typed view is
    // what samples the atlases its handles name.
    public RawEntry<Impostor> impostor;

    // The impostor is drawn while the placement's projected diameter is at least this; 0 never
    // drops it. Here rather than in the record so the cull reads no arena.
    public float impostorMinPixels;
};
