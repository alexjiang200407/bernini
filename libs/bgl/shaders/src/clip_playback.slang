import idl.Clip;

// Where a clip's playback sits at `time`, shared by every animated tier: phase advances along the
// clip at the authored sample rate scaled by the instance's rate. A looping clip wraps; a one-shot
// clip holds its last frame. `rate = 0` holds `phase` under any clock.
public float ClipFrames(Clip clip, float phase, float rate, float time)
{
    float frames = phase + time * rate * clip.sampleRate;
    float count = float(clip.frameCount);

    if (clip.loop != 0)
    {
        frames = fmod(frames, count);
        if (frames < 0.0)
        {
            frames += count;
        }
    }
    else
    {
        frames = clamp(frames, 0.0, count - 1.0);
    }

    return frames;
}

// The two frames of the clip a fractional position falls between, relative to the clip's own frame 0,
// and its blend weight. On a loop the upper frame wraps onto 0 rather than running past the last: a
// looping clip's last frame duplicates its first, so wrapping the index is what makes the crossing
// seamless instead of stuttering on the duplicate.
//
// Relative, not absolute, because the two tiers add different bases to it -- a texture row for VAT, a
// frame-major sample offset for a skinned rig.
public void ClipFrameIndices(Clip clip, float frames, out uint f0, out uint f1, out float weight)
{
    f0 = uint(frames);
    f1 = f0 + 1;

    if (clip.loop != 0)
    {
        f1 = (f1 == clip.frameCount) ? 0 : f1;
    }
    else
    {
        f1 = min(f1, clip.frameCount - 1);
    }

    weight = frames - float(f0);
}
