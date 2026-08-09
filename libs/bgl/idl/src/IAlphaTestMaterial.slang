public interface IAlphaTestMaterial
{
    float GetAlphaCutoff();

    // Mip 0 texel counts of the map carrying the alpha, so coverage machinery can relate a UV
    // footprint to texels.
    float2 GetBaseColorDimensions();

    // The alpha channel `bias` levels coarser than this pixel's own footprint, i.e. the local mean
    // the strands under it average to. Sharpening minified coverage needs a centre that follows the
    // content; a material's cutoff is a constant and cannot. Biased rather than taken at an explicit
    // level so it inherits the hardware's LOD choice, anisotropy included -- a centre sampled
    // isotropically sits at a different level than an anisotropic base colour on a grazing card, and
    // the difference reads as coverage that is not there.
    float GetBaseColorAlphaBiased(SamplerState.Handle s, float2 uv, float bias);
};
