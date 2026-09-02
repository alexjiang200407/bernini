// WGSL-lowering driver for lib.math.HashedAlpha. Fragment stage: the hash takes derivatives of the
// surface position, which no other stage may. See docs/slang_shaders.md.
import lib.math.HashedAlpha;

[shader("fragment")]
float4 main(float3 surfacePos: POSITION, float2 uv: TEXCOORD) : SV_Target
{
    let threshold = HashedAlphaThreshold(surfacePos, 1.0);
    let sharpened = SharpenMinifiedAlpha(0.5, 0.25, uv, float2(256.0, 256.0));
    return float4(threshold, sharpened, 0.0, 1.0);
}
