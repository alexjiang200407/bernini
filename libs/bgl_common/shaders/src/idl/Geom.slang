import RangeWithCount;
import Submesh;

// What a geometry-creating method produced and every placement from it shares. A placement names one
// by Entry<Geom> rather than holding a copy, so two placements of one mesh cannot disagree about
// where its submeshes are.
public struct Geom
{
    public RangeWithCount<Submesh> submeshes;
};
