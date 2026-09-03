import idl.BoneSample;
import idl.IPoseTables;
import idl.Range;
import idl.SkinnedBone;
import lib.types.ComputeBuffer;
import lib.types.RangeBuffer;

// The tables a walk reads and the storage it writes, passed rather than read from a uniform block:
// the two kernels agree on nothing else in theirs.
public struct PoseTables : IPoseTables
{
    public RangeBuffer<SkinnedBone> bones;
    public RangeBuffer<BoneSample> samples;
    public ComputeBuffer<float4> palettes;

    SkinnedBone GetBone(Range<SkinnedBone> range, uint index) { return bones.Get(range, index); }

    BoneSample GetSample(Range<BoneSample> range, uint index) { return samples.Get(range, index); }

    float4 GetPalette(uint slot) { return palettes[slot]; }

    void SetPalette(uint slot, float4 value) { palettes[slot] = value; }
};
