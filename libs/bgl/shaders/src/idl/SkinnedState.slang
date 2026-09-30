import BlendSlot;
import Constants;
import Entry;
import Range;
import Rig;

// A hero instance's playback record. Written at spawn, and rewritten in place only when something
// happens to the instance -- a crossfade starts, a parameter is retargeted -- never per frame: time
// is the only per-frame input, and it arrives through ViewData, not here. SkinnedTableState is the
// same tier's crowd record; the arena header that names the kind is what says which pose source a
// placement draws from.
public struct SkinnedState
{
    public Entry<Rig> rig;

    // The weighted slots the pose pass blends. A plain clip is slot 0 at weight one and the rest
    // weightless; a rewrite that changes a slot still live at prevTime ramps it down from now
    // rather than dropping it, so the pose at prevTime is what the old record gave.
    public BlendSlot slots[cBlendSlots];

    // This instance's slice of the view's bone-palette buffer, in float4s. Two palettes live in it
    // back to back -- the pose at ViewData::time first, then the pose at ViewData::prevTime
    // cFloat4sPerBone * boneCount later -- so the mesh shader can skin both and derive a motion
    // vector without a history buffer. Allocated at spawn and freed with the instance; a record of
    // this kind always owns one, which is why the crowd record is a kind of its own rather than this
    // one with the field left null.
    public Range<float4> palette;
};
