import types.SubmeshInstance;
import types.ComputeBuffer;
import types.PackedBuffer;
import types.EntryBuffer;
import types.RangeBuffer;
import idl.PsoType;
import idl.Mesh;
import idl.Submesh;
import idl.Constants;
import debug.dbg;

static const uint kPsoCount = uint(PsoType::kCount);

struct Uniforms
{
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

groupshared uint gLocalHistogram[kPsoCount];

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID, uint localIdx: SV_GroupIndex)
{
    if (localIdx < kPsoCount)
    {
        gLocalHistogram[localIdx] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    let instance = gUniforms.instanceBuffer[globalIdx];

    if (!instance.meshInstance.Null())
    {
        let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);
        let submesh = gUniforms.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);
        let psoType = submesh.pso;

        dbg_assert(psoType < kPsoCount, ErrorCode.kInvalidPsoType);

        if (psoType < kPsoCount)
        {
            InterlockedAdd(gLocalHistogram[psoType], 1u);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (localIdx < kPsoCount)
    {
        InterlockedAdd(gUniforms.outBuffer[localIdx], gLocalHistogram[localIdx]);
    }
}
