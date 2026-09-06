// WGSL-lowering driver for lib.anim.ramp.
import idl.Ramp;
import lib.anim.ramp;

RWStructuredBuffer<float> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    Ramp ramp = {};
    gOut[0] = RampAt(ramp, float(id.x));
}
