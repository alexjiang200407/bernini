import idl.ITonemapLut;
import lib.math.Tonemap;
import lib.types.Texture;

// The renderer's display LUT: Blender's AgX formation as the strip gen_agx_lut.py wrote, one
// bindless texture. Assembled at the call site like SurfaceEnv, for the same reflection reason.
public struct TonemapLut : ITonemapLut
{
    public Texture2D.Handle strip;
    public SamplerState.Handle sampler;

    float3 Sample(float3 coord)
    {
        // The strip is size^2 wide and size high, so its height is the LUT's edge.
        let taps = StripLutTaps3D(coord, uint(strip.GetDimensions().y));
        return lerp(strip.SampleLevel(sampler, taps.uv0, 0.0).rgb, strip.SampleLevel(sampler, taps.uv1, 0.0).rgb, taps.blend);
    }
};
