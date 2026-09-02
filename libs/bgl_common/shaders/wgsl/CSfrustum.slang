// WGSL-lowering driver for lib.math.frustum.
import lib.math.frustum;

StructuredBuffer<float4> gPlanes;
RWStructuredBuffer<uint> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float4 planes[6];
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        planes[i] = gPlanes[i];
    }
    gOut[0] = SphereInFrustum(planes, float3(id), 1.0) ? 1u : 0u;
}
