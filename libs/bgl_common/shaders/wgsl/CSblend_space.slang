// WGSL-lowering driver for lib.anim.blend_space.
import idl.Clip;
import lib.anim.blend_space;

RWStructuredBuffer<float> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    Clip clip = {};
    let t = float(id.x);
    gOut[0] = MemberSeconds(clip) + SpanWeight(0.0, 1.0, t) + PhaseElapsed(t, 1.0, 2.0);
}
