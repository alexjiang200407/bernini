// WGSL-lowering driver for lib.data.ViewData: a plain struct, checked for its layout.
import lib.data.ViewData;

ConstantBuffer<ViewData> gView;
RWStructuredBuffer<float4> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gOut[0] =
        mul(gView.viewProj, float4(gView.jitter, gView.time, 1.0)) + mul(gView.prevViewProj, float4(gView.prevJitter, gView.prevTime, 1.0));
}
