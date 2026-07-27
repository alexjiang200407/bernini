import types.ComputeBuffer;
import idl.Constants;
import idl.DrawIndirectArgs;
import idl.PsoType;

static const uint kPsoCount = uint(PsoType::kCount);

// Turns the inclusive scan of per-bucket meshlet counts into the draw record for each bucket, one
// thread per bucket. firstVertex is where that bucket's records begin; vertexCount goes back to
// zero because ExpandMeshlets uses it as the allocator that fills the region.

struct Uniforms
{
    ComputeBuffer<uint> meshletPrefixSum;
    ComputeBuffer<DrawIndirectArgs> drawArgs;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(kPsoCount, 1, 1)]
void main(uint idx: SV_GroupThreadID)
{
    let base = (idx == 0u) ? 0u : gUniforms.meshletPrefixSum[idx - 1u];

    gUniforms.drawArgs[idx].vertexCount.store(0u);
    gUniforms.drawArgs[idx].instanceCount = 1u;
    gUniforms.drawArgs[idx].firstVertex = base * cVerticesPerMeshletRecord;
    gUniforms.drawArgs[idx].firstInstance = 0u;
}
