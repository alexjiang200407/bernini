import Entry;
import Range;
import Rig;

// A hero instance's playback record, written once at spawn and never touched per frame -- time is
// the only per-frame input, and it arrives through ViewData, not here. SkinnedTableState is the same
// tier's crowd record; the arena header that names the kind is what says which pose source a
// placement draws from.
public struct SkinnedState
{
    public Entry<Rig> rig;

    public uint clip; // index into the rig's clip table

    // Frame position within the clip: the spawn phase offset under playback, and the frozen frame an
    // instance shows when `rate` is zero.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;

    // This instance's slice of the view's bone-palette buffer, in float4s. Two palettes live in it
    // back to back -- the pose at ViewData::time first, then the pose at ViewData::prevTime
    // cFloat4sPerBone * boneCount later -- so the mesh shader can skin both and derive a motion
    // vector without a history buffer. Allocated at spawn and freed with the instance; a record of
    // this kind always owns one, which is why the crowd record is a kind of its own rather than this
    // one with the field left null.
    public Range<float4> palette;
};
