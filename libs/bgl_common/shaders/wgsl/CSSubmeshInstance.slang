// WGSL-lowering driver for lib.data.SubmeshInstance: a plain offset struct, checked for its layout.
import lib.data.SubmeshInstance;

StructuredBuffer<SubmeshInstance> gInstances;
RWStructuredBuffer<uint> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let instance = gInstances[id.x];
    gOut[0] = instance.material.GetByteOffset();
}
