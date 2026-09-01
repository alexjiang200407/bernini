import Entry;
import Rig;

// A crowd instance's playback record: SkinnedState's playback fields and no palette, because the
// pose it draws is the rig's and belongs to no instance. Which of the two a placement holds is the
// pose source, and the arena header that names the kind is the only place that is written down.
public struct SkinnedTableState
{
    public Entry<Rig> rig;

    public uint clip; // index into the rig's clip table

    // Frame position within the clip: the spawn phase offset under playback, and the frozen frame an
    // instance shows when `rate` is zero.
    public float phase;

    // Play-rate multiplier on the clip's authored sampleRate.
    public float rate;
};
