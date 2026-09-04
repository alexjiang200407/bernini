// WGSL-lowering driver for lib.anim.foot_plant.
import lib.anim.foot_plant;

StructuredBuffer<float4> gIn;
RWStructuredBuffer<float4> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    float4x4 modelToWorld = float4x4(gIn[0], gIn[1], gIn[2], float4(0.0, 0.0, 0.0, 1.0));
    let plane = GroundPlaneInModel(normalize(gIn[3].xyz), gIn[4].xyz, modelToWorld);
    let ground = SampleGround(plane, normalize(gIn[5].xyz), float3(id));
    let sole = OnSolePlane(ground, gIn[6].xyz, normalize(gIn[7].xyz));

    float4x4 rootDelta;
    float4x4 midDelta;
    SolveTwoBone(gIn[8].xyz, gIn[9].xyz, gIn[10].xyz, sole, rootDelta, midDelta);

    gOut[0] = rootDelta[0];
    gOut[1] = midDelta[0];
}
