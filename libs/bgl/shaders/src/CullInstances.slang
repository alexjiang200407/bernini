import types.SubmeshInstance;
import types.PackedBuffer;
import types.EntryBuffer;
import types.RangeBuffer;
import types.ComputeBuffer;
import idl.Mesh;
import idl.Submesh;
import idl.CullView;
import idl.Constants;
import debug.dbg;

// One thread per instance decides frustum visibility and writes a per-view visibility word the
// counting sort and the transparent depth-key pass then read. CullView travels as buffer data, not
// cbuffer scalars: a compute cbuffer that also holds bindless handles drops its plain fields.

struct Uniforms
{
    ComputeBuffer<CullView> cullView;
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<Mesh> meshBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<uint> visibility;
    ComputeBuffer<uint> stats;
};

ConstantBuffer<Uniforms> gUniforms;

static const uint kStatTested = 0u;
static const uint kStatFrustumCulled = 1u;

bool SphereInFrustum(float4 planes[6], float3 center, float radius)
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
        {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    let instance = gUniforms.instanceBuffer[globalIdx];

    // Padding and dead slots name no mesh: never visible, never counted.
    if (instance.meshInstance.Null())
    {
        gUniforms.visibility[globalIdx] = 0u;
        return;
    }

    let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);

    dbg_assert(instance.submeshIndex < mesh.submeshes.count, ErrorCode.kInvalidSubmeshIndex, instance.submeshIndex, mesh.submeshes.count);

    let submesh = gUniforms.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    // World-space bounding sphere. The centre transforms like a vertex -- the same mul the forward
    // pass applies -- so cull and draw cannot disagree on where the instance is. The radius scales
    // by the largest transformed basis length, which is the exact max axis scale for a
    // translation-rotation-scale transform; a sheared transform could under-scale it, but instance
    // transforms here are TRS.
    let center = mul(mesh.transform, float4(submesh.boundingCenter, 1.0)).xyz;

    let ax = mul(mesh.transform, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    let ay = mul(mesh.transform, float4(0.0, 1.0, 0.0, 0.0)).xyz;
    let az = mul(mesh.transform, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    let maxScaleSq = max(dot(ax, ax), max(dot(ay, ay), dot(az, az)));
    let radius = submesh.boundingRadius * sqrt(maxScaleSq);

    let visible = SphereInFrustum(gUniforms.cullView[0].frustumPlanes, center, radius);

    gUniforms.visibility[globalIdx] = visible ? 1u : 0u;

    InterlockedAdd(gUniforms.stats[kStatTested], 1u);
    if (!visible)
    {
        InterlockedAdd(gUniforms.stats[kStatFrustumCulled], 1u);
    }
}
