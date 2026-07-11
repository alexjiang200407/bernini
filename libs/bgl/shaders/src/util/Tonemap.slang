
public float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 6th-order polynomial fit of the AgX sigmoid (Benjamin Wrensch).
float3 AgxContrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

public float3 AgX(float3 color)
{
    const float3x3 agxMat = float3x3(
        0.842479062253094,
        0.0423282422610123,
        0.0423756549057051,
        0.0784335999999992,
        0.878468636469772,
        0.0784336,
        0.0792237451477643,
        0.0791661274605434,
        0.879142973793104
    );

    const float3x3 agxMatInv = float3x3(
        1.19687900512017,
        -0.0528968517574562,
        -0.0529716355144438,
        -0.0980208811401368,
        1.15190312990417,
        -0.0980434501171241,
        -0.0990297440797205,
        -0.0989611768448433,
        1.15107367264116
    );

    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    // Inset, log2-encode into the [minEv, maxEv] range, normalize, then the sigmoid contrast curve.
    color = mul(color, agxMat);
    color = clamp(log2(max(color, 1e-10)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = AgxContrast(color);

    // Outset, then linearize the display-encoded result so the sRGB target re-encodes it.
    color = mul(color, agxMatInv);
    color = pow(max(color, 0.0), float3(2.2, 2.2, 2.2));

    return color;
}
