import idl.ITonemapLut;

// Blender's AgX Base sRGB view, as Blender 5.2 ships it: scene-linear Rec.709 into FilmLight
// E-Gamut, a 25-stop log2 encoding, the 57^3 formation LUT, and the Rec.1886 decode of what it
// returns. The LUT is Blender's own file (scripts/gen_agx_lut.py); everything around it is the
// OCIO config's `AgX Base Rec.1886` view transform written out. See docs/passes.md.

// The two bilinear taps and the blend that read a 3D LUT stored as a strip of `size` slices --
// slice b at x offset b * size, texel (r, g) inside it. Shared so a renderer supplying its own
// ITonemapLut does not re-derive the layout the generator wrote.
public struct StripLutTaps
{
    public float2 uv0;
    public float2 uv1;
    public float blend;
};

public StripLutTaps StripLutTaps3D(float3 coord, uint size)
{
    const float n = float(size);
    const float last = n - 1.0;

    const float slice = coord.b * last;
    const float s0 = floor(slice);
    const float s1 = min(s0 + 1.0, last);

    const float x = 0.5 + coord.r * last;
    const float y = (0.5 + coord.g * last) / n;

    StripLutTaps taps;
    taps.uv0 = float2((s0 * n + x) / (n * n), y);
    taps.uv1 = float2((s1 * n + x) / (n * n), y);
    taps.blend = slice - s0;
    return taps;
}

// OCIO's lg2 allocation: log2 over [minEv, maxEv] normalized to [0, 1], 25 stops.
static const float c_AgXMinEv = -12.47393;
static const float c_AgXMaxEv = 12.5260688117;

// Scene-linear Rec.709 to the coordinate the formation LUT is read at: FilmLight E-Gamut, log2
// encoded. Unclamped above, so a grade applied here can still pull a highlight back into range.
public float3 AgXLogEncode(float3 color)
{
    // Rec.709 -> CIE XYZ D65 -> Linear FilmLight E-Gamut, as one matrix in row-vector layout.
    const float3x3 rec709ToEGamut = float3x3(
        0.5593711304, 0.0762207051, 0.0655266974, 0.3047832593, 0.7879718287, 0.1645467499, 0.1358455635, 0.1358074780, 0.7699265344
    );

    float3 e = mul(color, rec709ToEGamut);
    return (log2(max(e, exp2(c_AgXMinEv))) - c_AgXMinEv) / (c_AgXMaxEv - c_AgXMinEv);
}

// AgXLogEncode's inverse, back to scene-linear Rec.709: what a grade ahead of a curve other than
// AgX's formation decodes through. Exact above the encode's floor, and unclamped like it.
public float3 AgXLogDecode(float3 coord)
{
    // Linear FilmLight E-Gamut -> Rec.709, the inverse of AgXLogEncode's matrix, row-vector layout.
    const float3x3 eGamutToRec709 = float3x3(
        1.9072481551, -0.1624978469, -0.1275929271, -0.6929663304, 1.3766552144, -0.2352384781, -0.2142817312, -0.2141573953, 1.3628314269
    );

    const float3 e = exp2(coord * (c_AgXMaxEv - c_AgXMinEv) + c_AgXMinEv);
    return mul(e, eGamutToRec709);
}

// The LUT read at an encoded coordinate, returned scene-linear for the sRGB target to re-encode.
public float3 AgXFormation<L : ITonemapLut>(float3 coord, L lut)
{
    float3 display = lut.Sample(saturate(coord));

    // The LUT formed a Rec.1886 image; linearize it so the sRGB target re-encodes it.
    return pow(max(display, 0.0), float3(2.4, 2.4, 2.4));
}

public float3 AgX<L : ITonemapLut>(float3 color, L lut)
{
    return AgXFormation(AgXLogEncode(color), lut);
}

// The stylized display curve: Gran Turismo's (Hajime Uchimura, CEDEC 2017), per channel, with its
// published parameters. A toe below `m`, the identity from `m` for `(P - m) * l / a` (0.312), then a
// shoulder that approaches `P` and never reaches it. Its result is display-linear, for the sRGB target to encode. See
// docs/passes.md.

static const float c_GtMaxBrightness = 1.0;   // P
static const float c_GtContrast = 1.0;        // a
static const float c_GtLinearStart = 0.22;    // m
static const float c_GtLinearLength = 0.4;    // l
static const float c_GtBlackTightness = 1.33; // c
static const float c_GtPedestal = 0.0;        // b

public float GranTurismo(float x)
{
    const float P = c_GtMaxBrightness;
    const float a = c_GtContrast;
    const float m = c_GtLinearStart;
    const float c = c_GtBlackTightness;
    const float b = c_GtPedestal;

    const float l0 = ((P - m) * c_GtLinearLength) / a;
    const float S0 = m + l0;
    const float S1 = m + a * l0;
    const float C2 = (a * P) / (P - S1);
    const float CP = -C2 / P;

    const float w0 = 1.0 - smoothstep(0.0, m, x);
    const float w2 = step(m + l0, x);
    const float w1 = 1.0 - w0 - w2;

    const float toe = m * pow(max(x / m, 1e-6), c) + b;
    const float shoulder = P - (P - S1) * exp(CP * (x - S0));
    const float linear = m + a * (x - m);

    return toe * w0 + linear * w1 + shoulder * w2;
}

public float3 GranTurismo(float3 color)
{
    return float3(GranTurismo(color.r), GranTurismo(color.g), GranTurismo(color.b));
}
