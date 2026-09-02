// WGSL-lowering driver for lib.math.brdf_lut.
import lib.math.brdf_lut;

RWStructuredBuffer<float2> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gOut[0] = IntegrateBRDF(max(float(id.x) / 64.0, 1e-4), float(id.y) / 64.0);
}
