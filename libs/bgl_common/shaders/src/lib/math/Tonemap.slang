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

public float3 AgX<L : ITonemapLut>(float3 color, L lut)
{
    // Rec.709 -> CIE XYZ D65 -> Linear FilmLight E-Gamut, as one matrix in row-vector layout.
    const float3x3 rec709ToEGamut = float3x3(
        0.5593711304, 0.0762207051, 0.0655266974, 0.3047832593, 0.7879718287, 0.1645467499, 0.1358455635, 0.1358074780, 0.7699265344
    );

    // OCIO's lg2 allocation: log2 over [minEv, maxEv] normalized to [0, 1], 25 stops.
    const float minEv = -12.47393;
    const float maxEv = 12.5260688117;

    float3 e = mul(color, rec709ToEGamut);
    float3 coord = (log2(max(e, exp2(minEv))) - minEv) / (maxEv - minEv);

    float3 display = lut.Sample(saturate(coord));

    // The LUT formed a Rec.1886 image; linearize it so the sRGB target re-encodes it.
    return pow(max(display, 0.0), float3(2.4, 2.4, 2.4));
}
