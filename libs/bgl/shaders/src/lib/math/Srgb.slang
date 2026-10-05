// The sRGB transfer function, per channel. An sRGB render target applies the encode on write, so a
// shader needs these only where it works on the encoded value itself.

public float3 SrgbToLinear(float3 c)
{
    const float3 low = c / 12.92;
    const float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return lerp(low, high, step(0.04045, c));
}

// @pre `c` is non-negative.
public float3 LinearToSrgb(float3 c)
{
    const float3 low = c * 12.92;
    const float3 high = 1.055 * pow(c, float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
    return lerp(low, high, step(0.0031308, c));
}
