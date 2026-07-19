import types.SubmeshInstance;
import types.PackedBuffer;
import types.EntryBuffer;
import types.ComputeBuffer;
import idl.Mesh;
import idl.PsoType;
import idl.Constants;

// Mirrors bgl::IsOccludeTransparentPso: the self-occluding buckets, which draw with a depth pre-pass
// and an Equal-tested colour pass. Their depthFunc is fixed-function, so they cannot share a pipeline
// with plain blend -- which is the only reason the sorted list is partitioned at all.
bool IsOccludePso(PsoType pso)
{
    return pso == PsoType::kTransparentOcclude_StaticMesh_PBR || pso == PsoType::kTransparentOcclude_StaticMesh_LoosePbr;
}

// Mirrors bgl::IsTransparentPso. A transparent instance is drawn from the depth-sorted list rather
// than its counting-sort bucket, so it is the only kind this pass keys.
bool IsTransparentPso(PsoType pso)
{
    return pso == PsoType::kTransparent_StaticMesh_PBR || pso == PsoType::kTransparent_StaticMesh_LoosePbr ||
           pso == PsoType::kTransparentOcclude_StaticMesh_PBR || pso == PsoType::kTransparentOcclude_StaticMesh_LoosePbr;
}

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;

    // (sortKey, instanceIndex) pairs, compacted: only transparent instances get a slot.
    ComputeBuffer<uint2> outEntries;
    ComputeBuffer<uint> outCount;

    float3 cameraPos;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let instance = gUniforms.instanceBuffer[globalIdx];

    // The buffer is padded to whole groups with default instances; those name no mesh.
    if (instance.meshInstance.Null())
    {
        return;
    }

    let pso = PsoType(instance.pso);

    if (!IsTransparentPso(pso))
    {
        return;
    }

    let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);

    // Where the instance's origin lands, transformed the same way the forward pass transforms a
    // vertex -- so this cannot disagree with the matrix convention the geometry is drawn with.
    float3 center = mul(mesh.transform, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 toCamera = center - gUniforms.cameraPos;

    // Squared distance is non-negative, so its bit pattern already orders like the float. Inverting
    // it makes an ascending radix sort emit farthest-first, which is the order blending needs.
    uint depth = ~asuint(dot(toCamera, toCamera));

    // The self-occluding instances take the low half of the key space so one ascending sort leaves
    // the list already split into [occluders][the rest], each half still farthest-first. That is what
    // lets the draw be three fixed indirect dispatches instead of a discovered run list -- and it is
    // the correct order too, since the occluders' pre-pass depth is what the rest then test against.
    // Costs the bottom bit of depth precision, which is far below what sorting distances needs.
    // Capped below cSortPadKey: the sort pads its tail with that value, and a key that reached it
    // could be displaced by padding into the drawn prefix -- an instance at the camera inverts to
    // exactly all-ones. The lost precision is one distance step at zero range.
    uint key = min((IsOccludePso(pso) ? 0u : 0x80000000u) | (depth >> 1u), cSortPadKey - 1u);

    uint slot;
    InterlockedAdd(gUniforms.outCount[0], 1u, slot);

    gUniforms.outEntries[slot] = uint2(key, globalIdx);
}
