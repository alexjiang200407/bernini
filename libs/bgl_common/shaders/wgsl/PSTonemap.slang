// WGSL-lowering driver for lib.math.Tonemap. Fragment stage because that is where the renderer
// runs it (programs/screen/PostProcess.slang), not because it takes derivatives -- it does not.
// The LUT here is a plain 2D texture: the first ITonemapLut with no bindless handle.
import idl.ITonemapLut;
import lib.math.Tonemap;

Texture2D gLut;
SamplerState gLinear;

struct BoundLut : ITonemapLut
{
    float3 Sample(float3 coord)
    {
        uint width;
        uint height;
        gLut.GetDimensions(width, height);
        let taps = StripLutTaps3D(coord, height);
        return lerp(gLut.SampleLevel(gLinear, taps.uv0, 0.0).rgb, gLut.SampleLevel(gLinear, taps.uv1, 0.0).rgb, taps.blend);
    }
};

[shader("fragment")]
float4 main(float3 color: COLOR) : SV_Target
{
    BoundLut lut;
    return float4(AgX(color, lut), 1.0);
}
