// THIS IS A FILE GENERATED FROM SkinnedClip.slang. DO NOT EDIT MANUALLY

// One clip's frames within its rig's sample pool. The pool is frame-major, so frame `f` begins at
// sample `firstSample + f * boneCount` -- boneCount belonging to the geom, not the clip.
//
// Frames span the closed interval [0, duration]: a looping clip's last frame duplicates its first,
// so playback must wrap over `frameCount - 1` rather than `frameCount` or it stutters by one frame.
public struct SkinnedClip
{
    public uint firstSample;
    public uint frameCount;

    // Authored frames per second; an instance's rate multiplies it.
    public float sampleRate;

    // Nonzero wraps phase over the clip; zero holds the last frame.
    public uint loop;
};
