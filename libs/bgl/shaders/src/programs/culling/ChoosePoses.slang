import idl.AutoPosedInstance;
import idl.BlendNode;
import idl.BlendSlot;
import idl.BlendSpaceSample;
import idl.Clip;
import idl.DominantFrames;
import idl.Constants;
import idl.CullView;
import idl.Geom;
import idl.InstanceLod;
import idl.InstancePose;
import idl.LodLevel;
import idl.MeshInstance;
import idl.MeshInstanceFlag;
import idl.PosePool;
import idl.Rig;
import idl.SkinnedAutoState;
import lib.anim.clip_playback;
import lib.anim.pose_walk;
import lib.anim.resolve_slots;
import lib.culling.lod_select;
import lib.math.frustum;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One thread per automatic placement, ahead of the camera's cull: chooses its level of detail as
// the cull would, then which source each drawn entry takes, and cuts a pose-pool slice for every
// placement with an entry drawn per instance. Per placement rather than per submesh instance, so the
// slice is cut once and every submesh of the placement draws the word written here -- the cull reads
// it for these placements instead of choosing again.
//
// A placement draws per instance only while it wants to -- visible, and as large on screen as its
// mesh's level 0, or the view's pose threshold on a mesh with one level -- and holds the right: it
// drew per instance last frame, or was granted it. One that wants it without the right asks, and
// GrantPoses hands out what the budget leaves. So no frame draws more than the budget, and one that
// holds the right keeps it while it wants it -- and while a crossfade of its own is in flight, which
// the table could only show as one of the two clips.

struct Uniforms
{
    UploadBuffer<uint> autoPlacements;
    EntryBuffer<MeshInstance> meshBuffer;
    EntryBuffer<Geom> geomBuffer;
    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;
    UploadBuffer<CullView> cullView;

    // The words the cull reads and writes, indexed by MeshInstance entry; an automatic placement's
    // is written here and only read there.
    ComputeBuffer<InstanceLod> lodPrevious;
    ComputeBuffer<InstanceLod> lodCurrent;

    ComputeBuffer<InstancePose> instancePose;
    ComputeBuffer<PosePool> pool;
    ComputeBuffer<AutoPosedInstance> posed;
    ComputeBuffer<uint> requests;

    // What the table entry draws: the record's dominant entry, resolved once here for every stage
    // that draws the placement from its table.
    ComputeBuffer<DominantFrames> dominantFrames;
    RangeBuffer<BlendNode> blendNodeBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BlendSpaceSample> blendSampleBuffer;

    uint placementCount;

    // The clock a crossfade in flight is judged at, and the two the dominant entry is resolved at.
    float time;
    float prevTime;

    // Where the pool's block of the palette arena starts, and how many pose-list entries there are.
    uint poolStart;
    uint budget;
};

ConstantBuffer<Uniforms> gUniforms;

// Whether the placement would draw per instance were it free to: visible, and as large on screen
// as the pose threshold, held by cLodHysteresis against coming in so it does not swap on a
// threshold. A mesh with levels swaps where it leaves level 0, whose hysteresis `chosen` carries.
bool WantsPerInstance(Geom geom, CullView view, float size, uint chosen, InstanceLod previous, bool visible)
{
    if (!visible || view.poseForced == cPoseForceTable)
    {
        return false;
    }
    if (view.poseForced == cPoseForcePerInstance)
    {
        return true;
    }
    if (geom.submeshes.lodCount > 1u)
    {
        return chosen == 0u;
    }

    let incumbent = !previous.Unset() && previous.DrawsPerInstance();
    let threshold = view.posePixels * view.lodPixelScale * (incumbent ? 1.0 : 1.0 + cLodHysteresis);
    return size >= threshold;
}

// Whether a crossfade is in flight at `time`: a slot whose weight is ramping. The table can hold one
// clip and two crossfading clips differ most mid-ramp, so a placement drawn per instance holds there
// until the ramp ends rather than dissolving onto a clip it is halfway out of.
bool CrossfadeInFlight(BlendSlot slots[cBlendSlots], float time)
{
    for (uint s = 0; s < cBlendSlots; ++s)
    {
        let slot = slots[s];
        if (slot.weight0 != slot.weight1 && time >= slot.rampStart && time < slot.rampEnd)
        {
            return true;
        }
    }
    return false;
}

// This frame's word. Its current entry is (level, source); a change of either dissolves out of the
// entry drawn until now -- a level and a source at once where the source swaps at level 0, the same
// level on two sources where only the source changes. A dissolve in progress advances by `step` and
// keeps its outgoing entry. `snap` makes any change at once. The grant bit is never carried: it is
// GrantPoses' to set for one frame.
InstanceLod ChooseWord(InstanceLod previous, uint level, bool fromTable, float step, bool snap)
{
    let current = LodLevel(level);
    let still = InstanceLod.MakeWithSources(current, fromTable, false, current, fromTable, 1.0);
    if (previous.Unset() || snap)
    {
        return still;
    }

    if (uint(previous.Level()) != level || previous.FromTable() != fromTable)
    {
        return step >= 1.0 ? still : InstanceLod.MakeWithSources(current, fromTable, true, previous.Level(), previous.FromTable(), step);
    }

    if (previous.Fading())
    {
        // Within one stored step of the end is the end, as AdvanceLod has it.
        let fade = previous.Fade() + step;
        return fade >= 1.0 - 1.0 / cInstanceLodFadeScale
                   ? still
                   : InstanceLod.MakeWithSources(current, fromTable, true, previous.OutgoingLevel(), previous.OutgoingFromTable(), fade);
    }
    return still;
}

// How far the placement's foot IK reaches this frame: in full while it draws per instance alone,
// rising with the dissolve in from its table and falling with the one out to it, so the feet reach
// the unplanted pose the table holds as it takes over.
float IKScale(InstanceLod word)
{
    if (!word.Fading() || word.FromTable() == word.OutgoingFromTable())
    {
        return 1.0;
    }
    return word.FromTable() ? 1.0 - word.Fade() : word.Fade();
}

// The global frames of the rig's clip set the record's heaviest entry falls between at `time`, and
// how far between them.
void DominantAt(SkinnedAutoState state, Rig rig, float time, out uint lower, out uint upper, out float weight)
{
    BlendTables tables;
    tables.nodes = gUniforms.blendNodeBuffer;
    tables.clips = gUniforms.clipBuffer;
    tables.samples = gUniforms.blendSampleBuffer;

    let resolved = ResolveSlots(tables, state.slots, rig, time);
    let entry = DominantEntry(resolved);
    let clip = resolved.clips[entry];

    uint f0;
    uint f1;
    ClipFrameIndices(clip, resolved.frames[entry], f0, f1, weight);
    lower = clip.firstFrame + f0;
    upper = clip.firstFrame + f1;
}

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint threadIdx: SV_DispatchThreadID)
{
    if (threadIdx >= gUniforms.placementCount)
    {
        return;
    }

    let placement = gUniforms.autoPlacements[threadIdx];
    let mesh = gUniforms.meshBuffer.Get(placement);
    let previous = gUniforms.lodPrevious[placement];

    InstancePose none;
    none.palette.offsetStart = 0u;

    // Hidden: drawn by nothing, posed by nothing, its word carried for when it comes back.
    if (HasMeshInstanceFlag(mesh, MeshInstanceFlag.kHidden))
    {
        gUniforms.lodCurrent[placement] = previous;
        gUniforms.instancePose[placement] = none;
        return;
    }

    let geom = gUniforms.geomBuffer.Get(mesh.geom);
    let view = gUniforms.cullView[0];

    let sphere = mesh.TransformSphere(geom.boundingSphere);
    let visible = SphereInFrustum(view.frustumPlanes, sphere.xyz, sphere.w);

    let size = ProjectedDiameter(mesh, geom, view);
    let chosen = ChooseLevel(geom, view, size, previous);
    let state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedAutoState>(mesh.playback);
    let rig = gUniforms.rigs.Get(state.rig);

    DominantFrames dominant;
    DominantAt(state, rig, gUniforms.time, dominant.lower, dominant.upper, dominant.weight);
    DominantAt(state, rig, gUniforms.prevTime, dominant.prevLower, dominant.prevUpper, dominant.prevWeight);
    gUniforms.dominantFrames[placement] = dominant;

    let incumbent = !previous.Unset() && previous.DrawsPerInstance();
    let holding = incumbent && visible && view.poseForced == cPoseForceNone && CrossfadeInFlight(state.slots, gUniforms.time);
    let wants = holding || WantsPerInstance(geom, view, size, chosen, previous, visible);
    let mayPose = incumbent || (!previous.Unset() && previous.Granted());

    if (wants && !mayPose)
    {
        gUniforms.requests[gUniforms.pool[0].requested.add(1u)] = placement;
    }

    // A placement no one sees, or one whose level is forced, changes at once: there is nothing to
    // hide, and a forced level is how a level is looked at on its own.
    let snap = !visible || view.lodForcedLevel != cLodForceNone;
    var word = ChooseWord(previous, chosen, !(wants && mayPose), view.lodFadeStep, snap);

    var pose = none;
    if (word.DrawsPerInstance())
    {
        // A hero palette's layout: the pose at time, the pose at prevTime, a sole per leg.
        let slice = cFloat4sPerBone * rig.boneCount * 2u + cFloat4sPerSole * rig.legs.count;
        let entry = gUniforms.pool[0].posed.add(1u);
        let offset = gUniforms.pool[0].cursor.add(slice);

        // The grants keep the placements posed within the budget, and the pool is sized to it.
        // Past either is only a budget lowered under placements that already held the right: they
        // draw from the table, and the counters overrun, which every reader clamps.
        if (entry < gUniforms.budget && offset + slice <= gUniforms.pool[0].capacity)
        {
            pose.palette.offsetStart = gUniforms.poolStart + offset;

            AutoPosedInstance posed;
            posed.mesh = placement;
            posed.footIK = state.footIK;
            posed.palette = pose.palette;
            posed.ikScale = IKScale(word);
            gUniforms.posed[entry] = posed;
        }
        else
        {
            word = InstanceLod.MakeWithSources(word.Level(), true, false, word.Level(), true, 1.0);
        }
    }

    gUniforms.lodCurrent[placement] = word;
    gUniforms.instancePose[placement] = pose;
}
