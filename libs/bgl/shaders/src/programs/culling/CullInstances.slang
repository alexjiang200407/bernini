import idl.Constants;
import idl.CullStats;
import idl.CullView;
import idl.DrawBucket;
import idl.Geom;
import idl.MeshInstanceFlag;
import idl.InstanceLod;
import idl.InstanceVisibility;
import idl.MeshInstance;
import idl.PlaybackType;
import idl.Submesh;
import lib.culling.lod_select;
import lib.culling.occlusion;
import lib.culling.visibility_bits;
import lib.debug.dbg;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.data.SubmeshInstance;
import lib.types.UploadBuffer;
import lib.math.frustum;

// One thread per instance decides frustum visibility and writes a per-view visibility word the
// counting sort and the transparent depth-key pass then read, and chooses its placement's level of
// detail (lib.culling.lod_select) -- which level draws, and whether a dissolve out of the last one
// draws beside it. CullView travels as buffer data, not
// cbuffer scalars: a compute cbuffer that also holds bindless handles drops its plain fields.
//
// This is phase 1 of the occlusion cull as well: an occludee (DrawBucketFlag.kOccludee) drawn
// last frame is tested against the HZB that frame left, through that frame's view-projection, and
// one it hides -- or one not drawn last frame -- is left as a candidate for CullOccluded, which
// tests it against the HZB phase 1's depth builds.

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

    // Read for the record's kind alone: an automatic placement's word was chosen by ChoosePoses,
    // which also chose its sources, and is read here rather than chosen again.
    RawBuffer playbackBuffer;

    // One DrawBucketFlag word per bucket: which buckets are occludees.
    UploadBuffer<uint> drawBucketFlags;

    // One word per instance slot, kept across frames: whether the slot drew last frame, in either
    // phase. Read here, written here for phase 1's verdict and by CullOccluded for phase 2's.
    ComputeBuffer<uint> drawnHistory;

    // The HZB ladder of this frustum's previous draw, one handle per level (cMaxHzbLevels), and
    // the clamping sampler its 2x2 is gathered through.
    SamplerState.Handle hzbSampler;
    Texture2D.Handle hzb0;
    Texture2D.Handle hzb1;
    Texture2D.Handle hzb2;
    Texture2D.Handle hzb3;
    Texture2D.Handle hzb4;
    Texture2D.Handle hzb5;
    Texture2D.Handle hzb6;
    Texture2D.Handle hzb7;
    Texture2D.Handle hzb8;
    Texture2D.Handle hzb9;
    Texture2D.Handle hzb10;
    Texture2D.Handle hzb11;

    // Profiling counters for the C5 overlay and the cull tests; the per-instance atomics are the only
    // real cost, so they are gated to BERNINI_GPU_DEBUG below.
    ComputeBuffer<CullStats> stats;
};

ConstantBuffer<Uniforms> gUniforms;

Texture2D.Handle HzbLevel(uint level)
{
    switch (level)
    {
    case 0u:
        return gUniforms.hzb0;
    case 1u:
        return gUniforms.hzb1;
    case 2u:
        return gUniforms.hzb2;
    case 3u:
        return gUniforms.hzb3;
    case 4u:
        return gUniforms.hzb4;
    case 5u:
        return gUniforms.hzb5;
    case 6u:
        return gUniforms.hzb6;
    case 7u:
        return gUniforms.hzb7;
    case 8u:
        return gUniforms.hzb8;
    case 9u:
        return gUniforms.hzb9;
    case 10u:
        return gUniforms.hzb10;
    default:
        return gUniforms.hzb11;
    }
}

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
        gUniforms.drawnHistory[globalIdx] = 0u;
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

    let automatic = !mesh.playback.Null() && PlaybackType(gUniforms.playbackBuffer.LoadTag(mesh.playback)) == PlaybackType.kSkinnedAuto;

    // A placement no one sees snaps to its level rather than dissolving toward it off screen.
    InstanceLod word;
    if (automatic)
    {
        word = gUniforms.lodCurrent[placement];
    }
    else
    {
        let chosen = ChooseLevel(geom, view, ProjectedDiameter(mesh, geom, view), previous);
        word = AdvanceLod(previous, chosen, view.lodFadeStep, !visible || view.lodForcedLevel != cLodForceNone);
        if (writesLod)
        {
            gUniforms.lodCurrent[placement] = word;
        }
    }

    // An occludee inside the frustum draws now only if it drew last frame and that frame's HZB
    // does not hide it; otherwise phase 2 decides. Without a valid ladder every occludee draws.
    let bucket = instance.drawBucket;
    let occludee = (view.occlusion & cOcclusionTestBit) != 0u && bucket < cMaxDrawBuckets &&
                   (gUniforms.drawBucketFlags[bucket] & uint(DrawBucketFlag.kOccludee)) != 0u;
    bool candidate = false;
    if (visible && occludee && (view.occlusion & cOcclusionHzbValidBit) != 0u)
    {
        if ((gUniforms.drawnHistory[globalIdx] & cDrawnMeshBit) == 0u)
        {
            candidate = true;
        }
        else
        {
            let query = PrepareHzbQuery(view, view.prevViewProj, sphere);
            if (!query.visible)
            {
                // The level differs from thread to thread, so the handle it picks is no uniform index.
                let gathered = nonuniform(HzbLevel(query.level)).GatherRed(gUniforms.hzbSampler, query.uv);
                candidate = HzbHides(gathered, query.nearest);
            }
        }
    }

    uint bits = candidate ? cVisibleCandidateBit : VisibilityBits(word, geom, visible);
    let meshDrawn = (bits & (cVisibleCurrentBit | cVisibleOutgoingBit)) != 0u;

    // Past the last level the placement draws its impostor, once, from its first submesh, tested
    // against the sphere over the whole of level 0 -- the impostor's quad spans it -- rather than
    // that submesh's own. An impostor is no occludee: a candidate's draws now, and phase 2 never
    // draws it again.
    if (writesLod && !automatic && !geom.impostor.Null())
    {
        let pastLast =
            uint(word.Level()) >= geom.submeshes.lodCount || (word.Fading() && uint(word.OutgoingLevel()) >= geom.submeshes.lodCount);
        let whole = mesh.TransformSphere(geom.boundingSphere);
        if (pastLast && ProjectedDiameter(mesh, geom, view) >= geom.impostorMinPixels * view.lodPixelScale &&
            SphereInFrustum(view.frustumPlanes, whole.xyz, whole.w))
        {
            bits |= cVisibleImpostorBit;
            if (word.Fading())
            {
                bits |= cVisibleDissolvingBit;
            }
        }
    }

    gUniforms.visibility[globalIdx].visible = bits;
    gUniforms.drawnHistory[globalIdx] = meshDrawn ? cDrawnMeshBit : 0u;

#if defined(BERNINI_GPU_DEBUG)
    gUniforms.stats[0].tested.add(1u);
    if (!visible)
    {
        gUniforms.stats[0].frustumCulled.add(1u);
    }
    if (candidate)
    {
        gUniforms.stats[0].occlusionCandidates.add(1u);
    }
#endif
}
