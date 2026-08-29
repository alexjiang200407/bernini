import idl.Rig;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;

// Buffer handles only, the same shape as VatData and for the same reason. The rig's bone and sample
// tables are absent: the geometry stage reads a *pose*, which SkinnedPosePass already resolved into
// bonePalettes -- re-deriving it per vertex is the work that pass exists to avoid.
public struct SkinnedData
{
    public EntryBuffer<Rig> rigBuffer;

    // Written by SkinnedPosePass this frame; read-only here.
    public ComputeBuffer<float4> bonePaletteBuffer;
};
