import bgl.InstanceWriter;
import lib.types.ComputeBuffer;
import InstanceWriterProbe;

// A fake behind IMeshInstanceBlock: one float4 per slot, the placed translation with w = 1, or w = -1
// for a hidden slot.
struct RecordingBlock : IMeshInstanceBlock
{
    ComputeBuffer<float4> slots;
    uint capacity;

    uint Capacity() { return capacity; }

    void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        slots[slot] = float4(transform[0].w, transform[1].w, transform[2].w, 1.0);
    }

    void Hide(uint slot) { slots[slot] = float4(0.0, 0.0, 0.0, -1.0); }
};

struct Uniforms
{
    RecordingBlock block;
    InstanceWriterProbe.Params params;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint slot: SV_DispatchThreadID)
{
    if (slot >= gUniforms.block.Capacity())
        return;
    InstanceWriterProbe.Write(gUniforms.block, gUniforms.params, slot);
}
