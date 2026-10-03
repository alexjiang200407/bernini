import bgl.MeshInstanceWriter;
import lib.types.ComputeBuffer;

// A fake behind both block interfaces: one float4 per slot, the placed translation with w = 1, or
// w = -1 for a hidden slot, and one float per slot, the playback offset it was last given.
public struct RecordingBlock : ISkinnedMeshInstanceBlock
{
    public ComputeBuffer<float4> slots;
    public ComputeBuffer<float> offsets;
    public uint capacity;

    public uint Capacity() { return capacity; }

    public void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        slots[slot] = float4(transform[0].w, transform[1].w, transform[2].w, 1.0);
    }

    public void Hide(uint slot) { slots[slot] = float4(0.0, 0.0, 0.0, -1.0); }

    public void SetPlaybackOffset(uint slot, float seconds) { offsets[slot] = seconds; }
};
