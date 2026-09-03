// WGSL-lowering driver for lib.math.motion_vector.
import lib.math.motion_vector;

RWStructuredBuffer<float2> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gOut[0] = ComputeMotionVector(float4(float3(id), 1.0), float4(0.0, 0.0, 0.0, 1.0)) + MotionFromNdc(float2(id.xy), float2(0.0, 0.0));
}
