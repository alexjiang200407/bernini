// WGSL-lowering driver for lib.anim.blend_slots.
import idl.BlendSlot;
import idl.Clip;
import lib.anim.blend_slots;

RWStructuredBuffer<float> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    Clip clip = {};
    BlendSlot slot = {};
    let time = float(id.x);
    gOut[0] = SlotWeight(slot, time) + SlotFrames(clip, slot, time);
}
