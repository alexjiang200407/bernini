import idl.Clip;
import idl.ClipPlayback;

// Where a clip's playback sits at `time`, shared by every animated tier: phase advances along the
// clip at the authored sample rate scaled by the instance's rate. A looping clip wraps; a one-shot
// clip holds its last frame. `rate = 0` holds `phase` under any clock.
//
// A loop wraps over `frameCount - 1`, not `frameCount`: the importer counts both ends, and marks a
// clip looping precisely because its last frame repeats its first, so one cycle is that many
// intervals. Wrapping over `frameCount` spends an extra interval blending the duplicate onto the
// frame it duplicates -- the clip holds still for one frame and runs slow by 1/(frameCount - 1).
public float ClipFrames(Clip clip, ClipPlayback playback, float time)
{
    float frames = playback.phase + time * playback.rate * clip.sampleRate;
    float count = float(clip.frameCount);

    if (clip.loop != 0)
    {
        // clip.loop is only ever set on a clip of more than one frame, so this cannot be zero.
        float cycle = count - 1.0;

        frames = fmod(frames, cycle);
        if (frames < 0.0)
        {
            frames += cycle;
        }
    }
    else
    {
        frames = clamp(frames, 0.0, count - 1.0);
    }

    return frames;
}

// The two frames of the clip a fractional position falls between, relative to the clip's own frame 0,
// and its blend weight. The upper index tops out at `frameCount - 1` on either kind of clip: on a
// loop that frame is the authored duplicate of frame 0, so the seam reads it rather than wrapping
// onto 0.
//
// Relative, not absolute, because the caller adds the base its own storage is addressed by -- the
// sample pool for a pose walk, the rig's bone anim table for a crowd instance.
public void ClipFrameIndices(Clip clip, float frames, out uint f0, out uint f1, out float weight)
{
    f0 = uint(frames);
    f1 = min(f0 + 1, clip.frameCount - 1);

    weight = frames - float(f0);
}
