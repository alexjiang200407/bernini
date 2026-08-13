// THIS IS A FILE GENERATED FROM VatClip.slang. DO NOT EDIT MANUALLY

// One clip's rows of a VAT texture pair. Frame `f` is row `firstRow + f`; the row after the last
// frame duplicates it, so fractional-V interpolation never bleeds into the clip stacked below.
public struct VatClip
{
    public uint firstRow;
    public uint frameCount;

    // Authored frames per second; an instance's rate multiplies it.
    public float sampleRate;

    // Nonzero wraps phase over the clip; zero holds the last frame.
    public uint loop;
};
