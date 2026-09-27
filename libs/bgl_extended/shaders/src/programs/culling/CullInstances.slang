import idl.Constants;
import idl.CullStats;
import idl.CullView;
import idl.Geom;
import idl.MeshInstanceFlag;
import idl.InstanceLod;
import idl.InstanceVisibility;
import idl.MeshInstance;
import idl.Submesh;
import lib.culling.lod_select;
import lib.debug.dbg;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.data.SubmeshInstance;
import lib.types.UploadBuffer;
import lib.math.frustum;

// One thread per instance decides frustum visibility and writes a per-view visibility word the
// counting sort and the transparent depth-key pass then read, and chooses its placement's level of
// detail (lib.culling.lod_select) -- which level draws, and whether a dissolve out of the last one
// draws beside it. CullView travels as buffer data, not
// cbuffer scalars: a compute cbuffer that also holds bindless handles drops its plain fields.

struct Uniforms
{
    UploadBuffer<CullView> cullView;
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<MeshInstance> meshBuffer;
    EntryBuffer<Geom> geomBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<InstanceVisibility> visibility;

    // Each placement's level of detail by MeshInstance entry: the word the last cull wrote, read,
    // and this cull's, written by the placement's submesh 0 alone.
    ComputeBuffer<InstanceLod> lodPrevious;
    ComputeBuffer<InstanceLod> lodCurrent;

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
    let placement = instance.meshInstance.GetOffset();
    let previous = gUniforms.lodPrevious[placement];
    let writesLod = instance.submeshIndex == 0u;

    // A hidden placement is live but drawn by nothing: never visible, and not counted as tested or
    // frustum-culled, since no frustum decided it. Its word is carried over, or the other buffer's
    // two-frame-old one would be read back next frame.
    if (HasMeshInstanceFlag(mesh, MeshInstanceFlag.kHidden))
    {
        if (writesLod)
        {
            gUniforms.lodCurrent[placement] = previous;
        }
        gUniforms.visibility[globalIdx].visible = 0u;
        return;
    }

    let geom = gUniforms.geomBuffer.Get(mesh.geom);

    dbg_assert(
        instance.submeshIndex < geom.submeshes.SubmeshCount(),
        ErrorCode.kInvalidSubmeshIndex,
        instance.submeshIndex,
        geom.submeshes.SubmeshCount()
    );

    let submesh = gUniforms.submeshBuffer.Get(geom.submeshes, geom.submeshes.EntryLod0(instance.submeshIndex));

    // The centre transforms like a vertex -- the same accessor the forward pass uses -- so cull and
    // draw cannot disagree on where the instance is.
    let sphere = mesh.TransformSphere(submesh.boundingSphere);

    let view = gUniforms.cullView[0];
    let visible = SphereInFrustum(view.frustumPlanes, sphere.xyz, sphere.w);

    // A placement no one sees snaps to its level rather than dissolving toward it off screen.
    let chosen = ChooseLevel(geom, view, ProjectedDiameter(mesh, geom, view), previous);
    let word = AdvanceLod(previous, chosen, view.lodFadeStep, !visible || view.lodForcedLevel != cLodForceNone);
    if (writesLod)
    {
        gUniforms.lodCurrent[placement] = word;
    }

    uint bits = 0u;
    if (visible && uint(word.Level()) < geom.submeshes.lodCount)
    {
        bits |= cVisibleCurrentBit;
    }
    if (visible && word.Fading() && uint(word.OutgoingLevel()) < geom.submeshes.lodCount)
    {
        bits |= cVisibleOutgoingBit;
    }
    gUniforms.visibility[globalIdx].visible = bits;

#if defined(BERNINI_GPU_DEBUG)
    gUniforms.stats[0].tested.add(1u);
    if (!visible)
    {
        gUniforms.stats[0].frustumCulled.add(1u);
    }
#endif
}
