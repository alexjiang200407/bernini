import types.ComputeBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import types.RangeBuffer;
import types.SubmeshInstance;
import idl.Constants;
import idl.InstanceVisibility;
import idl.Mesh;
import idl.PsoType;
import idl.Submesh;
import idl.ErrorCode;
import debug.dbg;

static const uint kPsoCount = uint(PsoType::kCount);

// HistogramInstances counts instances per PSO; this counts the *meshlets* those instances carry, so
// the scan of it gives each bucket a record region wide enough for everything ExpandMeshlets will
// append there. It must skip exactly what the culling chain skips, or a bucket's region is sized for
// geometry the expansion never writes and the next bucket starts inside the gap.

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<InstanceVisibility> visibility;
    AtomicComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared Atomic<uint> gLocalHistogram[kPsoCount];

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    if (localIdx < kPsoCount)
    {
        gLocalHistogram[localIdx].store(0u);
    }
    GroupMemoryBarrierWithGroupSync();

    // Guarded on the count rather than returning early: the barriers below must stay in uniform
    // control flow.
    if (globalIdx < gUniforms.instanceBuffer.GetCount())
    {
        let instance = gUniforms.instanceBuffer[globalIdx];

        if (!instance.meshInstance.Null() && gUniforms.visibility[globalIdx].visible != 0u)
        {
            let psoType = instance.pso;

            dbg_assert(psoType < kPsoCount, ErrorCode.kInvalidPsoType);

            if (psoType < kPsoCount)
            {
                let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);
                let submesh = gUniforms.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

                gLocalHistogram[psoType].add(submesh.GetMeshletCount());
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (localIdx < kPsoCount)
    {
        gUniforms.outBuffer[localIdx].add(gLocalHistogram[localIdx].load());
    }
}
