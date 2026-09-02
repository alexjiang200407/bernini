// WGSL-lowering driver for lib.math.Tonemap. Fragment stage because that is where the renderer
// runs it (programs/screen/PostProcess.slang), not because it takes derivatives -- it does not.
import lib.math.Tonemap;

[shader("fragment")]
float4 main(float3 color: COLOR) : SV_Target
{
    return float4(AgX(color), 1.0);
}
