import bgl.MeshInstanceWriter;
import lib.types.ComputeBuffer;

// A fake behind both block interfaces: four float4s per slot -- the placed transform's translation
// (w = 1, or -1 for a hidden slot) and its +z axis, then the same of its prevTransform -- and one
// float per slot, the playback offset it was last given.
public struct RecordingBlock : ISkinnedMeshInstanceBlock
{
    public ComputeBuffer<float4> slots;
    public ComputeBuffer<float> offsets;
    public uint capacity;

    public uint Capacity() { return capacity; }

    public void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        slots[slot * 4u + 0u] = float4(transform[0].w, transform[1].w, transform[2].w, 1.0);
        slots[slot * 4u + 1u] = float4(transform[0].z, transform[1].z, transform[2].z, 0.0);
        slots[slot * 4u + 2u] = float4(prevTransform[0].w, prevTransform[1].w, prevTransform[2].w, 1.0);
        slots[slot * 4u + 3u] = float4(prevTransform[0].z, prevTransform[1].z, prevTransform[2].z, 0.0);
    }

    public void Hide(uint slot) { slots[slot * 4u] = float4(0.0, 0.0, 0.0, -1.0); }

    public void SetPlaybackOffset(uint slot, float seconds) { offsets[slot] = seconds; }
};
