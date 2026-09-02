// One weighted slot of a hero instance's playback record: a node of its rig, where that node's
// playback sits, and the ramps that move its weight and its parameter over time. Time is the only
// per-frame input -- the slot holds what to evaluate, never the evaluated value -- so a pose at any
// clock is a pure function of the record, which is what keeps the pose at prevTime exact.
//
// `node` indexes the rig's node table. That table is implicit for now: node `n` is clip `n`, and an
// authored blend space is what will make it a table of its own.
public struct BlendSlot
{
    public uint node;

    // Where playback sat at `tRef`, in the node's own unit: frames of a clip. Advances from there
    // at `rate` times the clip's authored sampleRate.
    public float phase;
    public float rate;
    public float tRef;

    // The weight ramp: `weight0` before `rampStart`, `weight1` from `rampEnd`, linear between. An
    // end at or before its start is a step. Live weights are normalized across the record's slots
    // where they are read, so these need not sum to anything.
    public float weight0;
    public float weight1;
    public float rampStart;
    public float rampEnd;

    // The parameter ramp, with the same shape. Read by a blend-space node and ignored by a clip.
    public float param0;
    public float param1;
    public float paramStart;
    public float paramEnd;
};
