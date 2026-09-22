import idl.Constants;
import idl.CullStats;
import idl.CullView;
import idl.Geom;
import idl.MeshInstanceFlag;
import idl.InstanceVisibility;
import idl.MeshInstance;
import idl.Submesh;
import lib.debug.dbg;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.data.SubmeshInstance;
import lib.types.UploadBuffer;
import lib.math.frustum;

// One thread per instance decides frustum visibility and writes a per-view visibility word the
// counting sort and the transparent depth-key pass then read. CullView travels as buffer data, not
// cbuffer scalars: a compute cbuffer that also holds bindless handles drops its plain fields.

struct Uniforms
{
    UploadBuffer<CullView> cullView;
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<MeshInstance> meshBuffer;
    EntryBuffer<Geom> geomBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<InstanceVisibility> visibility;

    // Profiling counters for the C5 overlay and the cull tests; the per-instance atomics are the only
    // real cost, so they are gated to BERNINI_GPU_DEBUG below.
    ComputeBuffer<CullStats> stats;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let instance = gUniforms.instanceBuffer[globalIdx];

    // Padding and dead slots name no mesh: never visible, never counted.
    if (instance.meshInstance.Null())
    {
        gUniforms.visibility[globalIdx].visible = 0u;
        return;
    }

    let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);

    // A hidden placement is live but drawn by nothing: never visible, and not counted as tested or
    // frustum-culled, since no frustum decided it.
    if (HasMeshInstanceFlag(mesh, MeshInstanceFlag.kHidden))
    {
        gUniforms.visibility[globalIdx].visible = 0u;
        return;
    }

    let geom = gUniforms.geomBuffer.Get(mesh.geom);

    dbg_assert(instance.submeshIndex < geom.submeshes.count, ErrorCode.kInvalidSubmeshIndex, instance.submeshIndex, geom.submeshes.count);

    let submesh = gUniforms.submeshBuffer.Get(geom.submeshes, instance.submeshIndex);

    // The centre transforms like a vertex -- the same accessor the forward pass uses -- so cull and
    // draw cannot disagree on where the instance is.
    let sphere = mesh.TransformSphere(submesh.boundingSphere);

    let visible = SphereInFrustum(gUniforms.cullView[0].frustumPlanes, sphere.xyz, sphere.w);

    gUniforms.visibility[globalIdx].visible = visible ? 1u : 0u;

#if defined(BERNINI_GPU_DEBUG)
    gUniforms.stats[0].tested.add(1u);
    if (!visible)
    {
        gUniforms.stats[0].frustumCulled.add(1u);
    }
#endif
}
