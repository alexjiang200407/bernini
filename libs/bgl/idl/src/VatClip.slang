// One clip's rows of a VAT texture pair. Frame `f` is row `firstRow + f`; the row after the last
// frame duplicates it, so fractional-V interpolation never bleeds into the clip stacked below.
public struct VatClip
{
    public uint firstRow;
    public uint frameCount;

    // Unread until playback lands: a frozen frame needs no clock, but a layout bump later would
    // stale every baked record for two fields whose meaning is already fixed.
    public float sampleRate;
    public uint loop;
};
