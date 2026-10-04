import Entry;
import ToonShadingRig;

// A run of a view's placements that share one toon shading rig: a hero's is one placement, an
// instance block's every slot. Dense and CPU-authored, rebuilt when a rig is set, cleared or a
// holder deleted; the evaluation pass runs one thread per placement of every range, `firstThread`
// being the running count of placements before this range.
public struct ToonShadingRigRange
{
    // A MeshInstance entry; the run is [firstPlacement, firstPlacement + count).
    public uint firstPlacement;
    public uint count;
    public uint firstThread;
    public Entry<ToonShadingRig> rig;
};
