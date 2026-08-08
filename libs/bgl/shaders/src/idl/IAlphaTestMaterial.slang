// THIS IS A FILE GENERATED FROM IAlphaTestMaterial.slang. DO NOT EDIT MANUALLY

public interface IAlphaTestMaterial
{
    float GetAlphaCutoff();

    // Mip 0 texel counts of the map carrying the alpha, so coverage machinery can relate a UV
    // footprint to texels.
    float2 GetBaseColorDimensions();
};
