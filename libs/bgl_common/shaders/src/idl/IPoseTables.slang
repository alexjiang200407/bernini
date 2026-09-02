// THIS IS A FILE GENERATED FROM IPoseTables.slang. DO NOT EDIT MANUALLY

import BoneSample;
import IBonePalette;
import Range;
import SkinnedBone;

// What a hierarchy walk reads and the storage it writes. The walk is generic over this so it names
// no buffer type: the one implementation today wraps bindless handles, and a renderer without them
// supplies its own.
public interface IPoseTables : IBonePalette
{
    SkinnedBone GetBone(Range<SkinnedBone> bones, uint index);
    BoneSample GetSample(Range<BoneSample> samples, uint index);

    void SetPalette(uint slot, float4 value);
};
