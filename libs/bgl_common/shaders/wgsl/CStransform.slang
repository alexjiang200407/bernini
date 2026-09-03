// WGSL-lowering driver for lib.math.transform.
import lib.math.transform;

StructuredBuffer<float4> gIn;
RWStructuredBuffer<float4> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float4x4 m = float4x4(gIn[0], gIn[1], gIn[2], float4(0.0, 0.0, 0.0, 1.0));
    let inverse = AffineInverse(m);
    let turn = RotationAbout(gIn[3].xyz, normalize(gIn[4].xyz), gIn[4].w);

    gOut[0] = float4(TransformPoint(inverse, float3(id)), 0.0);
    gOut[1] = float4(TransformDir(turn, float3(id)), 0.0);
}
