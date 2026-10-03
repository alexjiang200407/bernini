import bgl.MeshInstanceWriter;
import lib.types.ComputeBuffer;
import MeshInstanceWriterProbe;

// A fake behind IMeshInstanceBlock: one float4 per slot, the placed translation with w = 1, or w = -1
// for a hidden slot, and one float per slot, the playback offset it was last given.
struct RecordingBlock : IMeshInstanceBlock
{
    ComputeBuffer<float4> slots;
    ComputeBuffer<float> offsets;
    uint capacity;

    uint Capacity() { return capacity; }

    void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        slots[slot] = float4(transform[0].w, transform[1].w, transform[2].w, 1.0);
    }

    void Hide(uint slot) { slots[slot] = float4(0.0, 0.0, 0.0, -1.0); }

    void SetPlaybackOffset(uint slot, float seconds) { offsets[slot] = seconds; }
};

struct Uniforms
{
    RecordingBlock block;
    MeshInstanceWriterProbe.Params params;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint slot: SV_DispatchThreadID)
{
    if (slot >= gUniforms.block.Capacity())
        return;
    MeshInstanceWriterProbe.Write(gUniforms.block, gUniforms.params, slot);
}
