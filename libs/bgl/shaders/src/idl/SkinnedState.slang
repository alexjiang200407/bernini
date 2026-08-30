// THIS IS A FILE GENERATED FROM SkinnedState.slang. DO NOT EDIT MANUALLY

import Entry;
import Range;
import SkinnedGeom;

// A skinned instance's playback record, written once at spawn and never touched per frame -- time is
// the only per-frame input, and it arrives through ViewData, not here. VatState's counterpart for the
// tier that computes its pose instead of fetching one.
public struct SkinnedState
{
    // The instance's world transform, the same one its Mesh record holds. Fixed for the instance's
    // lifetime, so a spawn-time constant like the fields below; first, because the widest member
    // has to be for the MSL and C++ layouts to agree.
    public float4x4 transform;

    public Entry<SkinnedGeom> geom;

    public uint clip; // index into the geom's clip table

    // Frame position within the clip: the spawn phase offset under playback, and the frozen frame an
    // instance shows when `rate` is zero.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;

    // This instance's slice of the view's bone-palette buffer, in float4s. Two palettes live in it
    // back to back -- the pose at ViewData::time first, then the pose at ViewData::prevTime
    // cFloat4sPerBone * boneCount later -- so the mesh shader can skin both and derive a motion
    // vector without a history buffer. Allocated at spawn and freed with the instance.
    public Range<float4> palette;
};
