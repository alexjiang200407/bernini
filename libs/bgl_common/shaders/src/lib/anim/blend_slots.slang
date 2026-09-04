import idl.BlendSlot;
import idl.Clip;
import lib.anim.clip_playback;

// What one BlendSlot evaluates to at a clock time. Pure functions of the slot and the time, which is
// what lets the pose pass read the same record at `time` and at `prevTime` and get the pair a motion
// vector needs.

// The slot's weight: `weight0` before its ramp, `weight1` from its end, linear between. A ramp whose
// end is not after its start is a step at the start.
public float SlotWeight(BlendSlot slot, float time)
{
    if (time < slot.rampStart)
    {
        return slot.weight0;
    }
    if (time >= slot.rampEnd)
    {
        return slot.weight1;
    }
    return lerp(slot.weight0, slot.weight1, (time - slot.rampStart) / (slot.rampEnd - slot.rampStart));
}

// Where the slot's clip has reached: its phase, advanced from `tRef`.
public float SlotFrames(Clip clip, BlendSlot slot, float time)
{
    return ClipFramesAfter(clip, slot.phase, slot.rate, time - slot.tRef);
}
