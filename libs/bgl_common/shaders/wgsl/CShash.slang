// WGSL-lowering driver for lib.math.hash.
import lib.math.hash;

RWStructuredBuffer<float> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gOut[0] = HashFloat(HashUint(id.x));
}
