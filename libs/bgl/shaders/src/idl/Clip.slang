// THIS IS A FILE GENERATED FROM Clip.slang. DO NOT EDIT MANUALLY

// One playable clip, shared by every animated tier. `firstFrame` is where the clip's frame 0 sits in
// whatever the tier addresses frames by -- a texture row for VAT, a frame of the frame-major sample
// pool for a skinned rig -- so the three fields below mean the same thing to both, which is what lets
// per-clip metadata grow in one place.
//
// Frames span the closed interval [0, duration]: a looping clip's last frame duplicates its first, so
// playback wraps over `frameCount - 1` rather than `frameCount` or it stutters by one frame.
public struct Clip
{
    public uint firstFrame;
    public uint frameCount;

    // Authored frames per second; an instance's rate multiplies it.
    public float sampleRate;

    // Nonzero wraps phase over the clip; zero holds the last frame.
    public uint loop;
};
