// WGSL-lowering driver for lib.math.box_bounds.
import lib.math.box_bounds;

StructuredBuffer<float4> gIn;
RWStructuredBuffer<float4> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float4x4 viewProj = float4x4(gIn[0], gIn[1], gIn[2], gIn[3]);
    float3 corners[cBoxCorners];
    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        corners[i] = gIn[4 + i].xyz + float3(id);
    }

    let rect = ProjectBoxBounds(viewProj, corners);
    gOut[0] = rect.IsEmpty() ? float4(0.0, 0.0, 0.0, 0.0) : float4(rect.lo, rect.hi);
}
