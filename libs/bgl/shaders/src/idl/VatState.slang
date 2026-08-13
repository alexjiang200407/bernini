// THIS IS A FILE GENERATED FROM VatState.slang. DO NOT EDIT MANUALLY

import Entry;
import VatGeom;

// A VAT instance's playback record, written once at spawn and never touched per frame -- time is
// the only per-frame input, and it arrives through ViewData, not here.
public struct VatState
{
    public Entry<VatGeom> geom;

    public uint clip; // index into the geom's clip table

    // Frame position within the clip. With no clock yet this is the frozen frame an instance
    // shows; under playback it becomes the spawn phase offset.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;
};
