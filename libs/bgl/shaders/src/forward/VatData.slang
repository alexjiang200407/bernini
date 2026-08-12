import idl.VatClip;
import idl.VatGeom;
import idl.VatState;
import types.EntryBuffer;
import types.RangeBuffer;

// Buffer handles only, exactly the shape ForwardData proves: a sampler here would make this a
// Mixed-category parameter in the mesh stage, which the Metal stage-binding path has never had to
// carry. The fetches Load by texel instead -- U is always an exact column, and a fractional frame
// is two Loads and a lerp when playback needs one.
public struct VatData
{
    public EntryBuffer<VatGeom> vatGeomBuffer;
    public EntryBuffer<VatState> vatStateBuffer;
    public RangeBuffer<VatClip> vatClipBuffer;
    public RangeBuffer<uint> vatColumnBuffer;
};
