import idl.Constants;
import idl.CullStats;
import idl.CullView;
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

    dbg_assert(instance.submeshIndex < mesh.submeshes.count, ErrorCode.kInvalidSubmeshIndex, instance.submeshIndex, mesh.submeshes.count);

    let submesh = gUniforms.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    // World-space bounding sphere. The centre transforms like a vertex -- the same accessor the
    // forward pass uses -- so cull and draw cannot disagree on where the instance is. The radius scales
    // by the largest transformed basis length, which is the exact max axis scale for a
    // translation-rotation-scale transform; a sheared transform could under-scale it, but instance
    // transforms here are TRS.
    let center = mesh.TransformPoint(submesh.boundingSphere.xyz);

    let ax = mesh.TransformDirection(float3(1.0, 0.0, 0.0));
    let ay = mesh.TransformDirection(float3(0.0, 1.0, 0.0));
    let az = mesh.TransformDirection(float3(0.0, 0.0, 1.0));
    let maxScaleSq = max(dot(ax, ax), max(dot(ay, ay), dot(az, az)));
    let radius = submesh.boundingSphere.w * sqrt(maxScaleSq);

    let visible = SphereInFrustum(gUniforms.cullView[0].frustumPlanes, center, radius);

    gUniforms.visibility[globalIdx].visible = visible ? 1u : 0u;

#if defined(BERNINI_GPU_DEBUG)
    gUniforms.stats[0].tested.add(1u);
    if (!visible)
    {
        gUniforms.stats[0].frustumCulled.add(1u);
    }
#endif
}
