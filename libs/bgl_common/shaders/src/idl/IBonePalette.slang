// THIS IS A FILE GENERATED FROM IBonePalette.slang. DO NOT EDIT MANUALLY

// A palette as the skinning math reads it: one bone's three rows, by slot. The pose walk writes
// through the wider IPoseTables; the vertex blend only ever reads, and is generic over this.
public interface IBonePalette
{
    float4 GetPalette(uint slot);
};
