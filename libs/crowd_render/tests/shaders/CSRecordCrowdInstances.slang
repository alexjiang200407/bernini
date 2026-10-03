import bgl.MeshInstanceWriter;
import crowd_render.CrowdInstanceWriter;
import lib.types.ComputeBuffer;

// A fake behind IMeshInstanceBlock: four float4s per slot -- the placed transform's translation
// (w = 1, or -1 for a hidden slot) and its +z axis, then the same of its prevTransform.
struct RecordingBlock : IMeshInstanceBlock
{
    ComputeBuffer<float4> slots;
    uint capacity;

    uint Capacity() { return capacity; }

    void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        slots[slot * 4u + 0u] = float4(transform[0].w, transform[1].w, transform[2].w, 1.0);
        slots[slot * 4u + 1u] = float4(transform[0].z, transform[1].z, transform[2].z, 0.0);
        slots[slot * 4u + 2u] = float4(prevTransform[0].w, prevTransform[1].w, prevTransform[2].w, 1.0);
        slots[slot * 4u + 3u] = float4(prevTransform[0].z, prevTransform[1].z, prevTransform[2].z, 0.0);
    }

    void Hide(uint slot) { slots[slot * 4u] = float4(0.0, 0.0, 0.0, -1.0); }

    void SetPlaybackOffset(uint slot, float seconds) {}
};

struct Uniforms
{
    RecordingBlock block;
    CrowdInstanceWriter.Params params;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint slot: SV_DispatchThreadID)
{
    if (slot >= gUniforms.block.Capacity())
        return;
    CrowdInstanceWriter.Write(gUniforms.block, gUniforms.params, slot);
}
