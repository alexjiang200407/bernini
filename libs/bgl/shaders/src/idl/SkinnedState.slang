// THIS IS A FILE GENERATED FROM SkinnedState.slang. DO NOT EDIT MANUALLY

import Entry;
import SkinnedGeom;

// A skinned instance's playback record, written once at spawn and never touched per frame -- time is
// the only per-frame input, and it arrives through ViewData, not here. VatState's counterpart for the
// tier that computes its pose instead of fetching one.
public struct SkinnedState
{
    public Entry<SkinnedGeom> geom;

    public uint clip; // index into the geom's clip table

    // Frame position within the clip: the spawn phase offset under playback, and the frozen frame an
    // instance shows when `rate` is zero.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;

    // Where this instance's bone palette begins, in float4s. Two palettes live there back to back --
    // the pose at ViewData::time first, then the pose at ViewData::prevTime cFloat4sPerBone *
    // boneCount later -- so the mesh shader can skin both and derive a motion vector without a
    // history buffer. Allocated at spawn and freed with the instance.
    public uint paletteBase;
};
