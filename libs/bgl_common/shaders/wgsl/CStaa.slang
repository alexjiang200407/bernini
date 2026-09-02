// WGSL-lowering driver for lib.math.taa.
import lib.math.taa;

RWStructuredBuffer<float4> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let c = float3(id) * 0.1;
    let y = RgbToYCoCg(ToneWeight(c));
    let back = UnToneWeight(YCoCgToRgb(y));
    let w = PhaseWeight(0.25, 0.125, 2.0, 1.0);
    let m = CameraMotion(float2(0.5, 0.5), 0.5, float2(0.0, 0.0), float4x4(1.0), float4x4(1.0));
    gOut[0] = float4(back, w) + float4(m, 0.0, 0.0);
}
