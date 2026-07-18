import types.SubmeshInstance;
import types.PackedBuffer;
import types.EntryBuffer;
import types.ComputeBuffer;
import idl.Mesh;
import idl.PsoType;
import idl.Constants;

// Mirrors bgl::IsTransparentPso. A transparent instance is drawn from the depth-sorted list rather
// than its counting-sort bucket, so it is the only kind this pass keys.
bool IsTransparentPso(uint pso)
{
    return pso == uint(PsoType::kTransparent_StaticMesh_PBR) || pso == uint(PsoType::kTransparent_StaticMesh_LoosePbr) ||
           pso == uint(PsoType::kTransparentOcclude_StaticMesh_PBR) || pso == uint(PsoType::kTransparentOcclude_StaticMesh_LoosePbr);
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

    if (!IsTransparentPso(instance.pso))
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
    uint key = ~asuint(dot(toCamera, toCamera));

    uint slot;
    InterlockedAdd(gUniforms.outCount[0], 1u, slot);

    gUniforms.outEntries[slot] = uint2(key, globalIdx);
}
