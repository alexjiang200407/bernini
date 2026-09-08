// The display curve's lookup, as the tone map reads it: one fetch at a coordinate in [0, 1]^3 of
// the LUT's own log-encoded space. The math is generic over this so it names no texture; the one
// implementation today samples a bindless strip, and a renderer without bindless supplies its own.
public interface ITonemapLut
{
    float3 Sample(float3 coord);
};
