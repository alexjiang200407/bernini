import Entry;
import Rig;

// Where an instance's playback sits: which rig, which of its clips, and how far into it. Both pose
// sources carry exactly this and differ only in whether a palette follows it, so a reader that
// needs a frame needs this and nothing else -- which is what lets ClipFrames take one argument
// rather than three that could come from different records.
public struct ClipPlayback
{
    public Entry<Rig> rig;

    public uint clip; // index into the rig's clip table

    // Frame position within the clip: the spawn phase offset under playback, and the frozen frame an
    // instance shows when `rate` is zero.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;
};
