import idl.Clip;
import idl.Rig;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.RangeBuffer;

// Buffer handles only, so a draw binds one constant buffer whatever it reads. The rig's bone and sample
// tables are absent: the geometry stage reads a *pose*, never derives one -- which is the work the
// two pose passes exist to do.
public struct SkinnedData
{
    public EntryBuffer<Rig> rigBuffer;

    // Written by SkinnedPosePass this frame; read-only here. Holds the pose of every instance whose
    // source is kPerInstance, and nothing for the rest.
    public ComputeBuffer<float4> bonePaletteBuffer;

    // Written by RigFramesPass, once per rig rather than once per frame; read-only here. The crowd
    // path reads a pose out of this instead of out of a slice of its own.
    public ComputeBuffer<float4> boneAnimTables;

    // Only the crowd path reads it: deriving a frame from {clip, phase, rate} and the clock is what
    // SkinnedPosePass already did for an instance posed per frame.
    public RangeBuffer<Clip> clipBuffer;
};
