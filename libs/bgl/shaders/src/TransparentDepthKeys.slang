import types.SubmeshInstance;
import types.PackedBuffer;
import types.EntryBuffer;
import types.ComputeBuffer;
import idl.Mesh;
import idl.PsoType;
import idl.Constants;
import idl.InstanceVisibility;

// Mirrors bgl::IsTransparentPso. A transparent instance is drawn from the depth-sorted list rather
// than its counting-sort bucket, so it is the only kind this pass keys.
bool IsTransparentPso(PsoType pso)
{
    return pso == PsoType::kTransparent_StaticMesh_PBR || pso == PsoType::kTransparent_StaticMesh_LoosePbr ||
           pso == PsoType::kTransparent_SkinnedMesh_PBR;
}

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    ComputeBuffer<InstanceVisibility> visibility;

    // (sortKey, instanceIndex) pairs, compacted: only transparent instances get a slot.
    ComputeBuffer<uint2> outEntries;
    AtomicComputeBuffer<uint> outCount;

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

    // A frustum-culled instance is drawn by nothing, so it must not take a slot in the sorted list.
    if (gUniforms.visibility[globalIdx].visible == 0u)
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

    // Capped below cSortPadKey: the sort pads its tail with that value, and a key that reached it
    // could be displaced by padding into the drawn prefix -- an instance at the camera inverts to
    // exactly all-ones. The lost precision is one distance step at zero range.
    uint key = min(depth, cSortPadKey - 1u);

    let slot = gUniforms.outCount[0].add(1u);

    gUniforms.outEntries[slot] = uint2(key, globalIdx);
}
