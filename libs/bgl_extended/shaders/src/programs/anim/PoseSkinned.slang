import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPlayback;
import idl.MeshInstance;
import idl.RawEntry;
import idl.Rig;
import idl.SkinnedBone;
import idl.SkinnedLegChain;
import idl.SkinnedState;
import lib.anim.clip_playback;
import lib.anim.foot_plant;
import lib.anim.pose_walk;
import lib.math.transform;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PoseTables;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One workgroup poses one skinned instance: sample its clip, walk the hierarchy, plant whatever feet
// the rig authored, multiply through the inverse bind, write the palette. Twice over -- once at
// `time` and once at `prevTime` -- so the mesh shader can skin both and derive a motion vector
// without a history buffer.
//
// The walk itself is pose_walk's, shared with PoseRigFrames. What is this kernel's own is where a
// pose comes from -- an instance's clip, phase and rate against the frame's clock -- and the foot
// plant, which a shared bone anim table cannot have: one table serves every instance of a rig, and a
// foot is planted against the ground under one instance in the world.

struct Uniforms
{
    // The mesh instances to pose, one per workgroup. Dense: erasing a placement only releases its
    // bytes, so a sweep of the playback arena would pose freed instances -- and would meet the crowd
    // records sharing it, which own no palette for this pass to write.
    UploadBuffer<uint> posedInstances;

    // Where an instance stands and which record it plays from. Both are read from the placement
    // rather than copied into the playback record: a foot is planted against the ground under the
    // instance in the world, and one transform in two records is one that can disagree with itself.
    EntryBuffer<MeshInstance> meshBuffer;

    RawBuffer playbackBuffer;
    EntryBuffer<Rig> rigs;

    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BoneSample> sampleBuffer;

    // Read only by a rig that authored legs: an avatar's chains, and one plant-weight byte per leg
    // per frame of the sample pool packed four to a uint.
    RangeBuffer<SkinnedLegChain> legBuffer;
    RangeBuffer<uint> plantWeightBuffer;

    ComputeBuffer<float4> bonePalettes;

    float time;
    float prevTime;
    uint posedCount;

    // The scene's ground plane, world space, normal unit length: what a planted foot is solved
    // against.
    float3 groundPoint;
    float3 groundNormal;

    // 0 when the scene has planting off: a rig with legs then poses exactly as one without.
    uint plantFeet;
};

ConstantBuffer<Uniforms> gUniforms;

// --- Solving a chain in the model-space window ------------------------------------------------------------------------------
//
// Runs in the model-space window: after the hierarchy walk every palette slot holds a bone's model
// transform, and before the inverse bind is folded in they still do. Nothing else in the frame has
// them, which is why the solve lives here rather than in a pass of its own.

// Where a solved chain's bones are and how each one moved, so CarrySolvedDescendants can read a
// delta the solving thread computed. Shared by every kind of solve that runs in this window, not
// just by legs -- which is the whole reason it is a table rather than three locals.
//
// Sized by cMaxLegsPerRig rather than by the bone count: a bone-count-sized array is what once
// capped a rig at 192. A second kind of solve raises the bound rather than adding an array.
groupshared uint gSolvedBone[cMaxLegsPerRig * 3];
groupshared float4x4 gSolvedDelta[cMaxLegsPerRig * 3];
groupshared uint gShallowestSolved;

// The largest distance any leg falls short of its target, as the bits of a float. Interlocked ops
// are integer-only, and a non-negative float's bit pattern orders exactly like the float, so a max
// over the bits is a max over the values -- which holds only because a deficit is never negative.
groupshared uint gMaxDeficit;

// The three rows a placement stores, as the affine matrix the plane pullback and the inverse below
// take. The fourth row is always (0,0,0,1) and is not stored.
// How planted leg `leg` is at a fractional frame, 0 to 1. Blended between the two frames the
// position falls between, like every other per-frame quantity here: a weight that stepped would put
// the pop back that the cook's ramp exists to remove.
float PlantWeight(Rig rig, Clip clip, uint leg, float frames)
{
    uint f0;
    uint f1;
    float blend;
    ClipFrameIndices(clip, frames, f0, f1, blend);

    let legs = rig.legs.count;
    let a = (clip.firstFrame + f0) * legs + leg;
    let b = (clip.firstFrame + f1) * legs + leg;

    let wordA = gUniforms.plantWeightBuffer.Get(rig.plantWeights, a / 4);
    let wordB = gUniforms.plantWeightBuffer.Get(rig.plantWeights, b / 4);

    let byteA = float((wordA >> (8 * (a % 4))) & 0xffu);
    let byteB = float((wordB >> (8 * (b % 4))) & 0xffu);

    return lerp(byteA, byteB, blend) * (1.0 / 255.0);
}

// Which entry of the solved table `bone` is, or -1. A linear scan over at most cMaxLegsPerRig * 3
// entries: the alternative is a per-bone tag, and a bone-count-sized array is what this pass
// deliberately does not keep.
int SolvedIndexOf(uint solved, uint bone)
{
    for (uint i = 0; i < solved; ++i)
    {
        if (gSolvedBone[i] == bone)
        {
            return int(i);
        }
    }
    return -1;
}

// One leg as the solve needs it: where its three joints are, where its ankle wants to be, and how
// far past the chain's reach that is.
//
// Read once and used twice -- to measure the deficit, then to solve after the rig has dropped -- so
// the target the drop is sized against and the target that is solved for cannot disagree.
struct LegPose
{
    float3 hip;
    float3 knee;
    float3 ankle;
    float3 target;
    float weight;

    // The turn the foot takes about its ankle: the ground's slope, weighted and clamped. Known
    // before the solve, since nothing the solve does changes the ground.
    float3 turnAxis;
    float turnAngle;

    // 0 when the chain can reach. A leg at weight 0 targets where its ankle already is, so it is
    // reachable by construction and contributes nothing to the drop.
    float deficit;
};

LegPose ReadLeg(
    PoseTables tables,
    Rig rig,
    Clip clip,
    SkinnedLegChain leg,
    uint index,
    float frames,
    float4 plane,
    float4 authored,
    float3 down,
    uint paletteBase
)
{
    LegPose pose;

    let hipModel = PaletteMatrix(tables, paletteBase + leg.hip * cFloat4sPerBone);
    let kneeModel = PaletteMatrix(tables, paletteBase + leg.knee * cFloat4sPerBone);
    let ankleModel = PaletteMatrix(tables, paletteBase + leg.ankle * cFloat4sPerBone);

    pose.hip = float3(hipModel[0].w, hipModel[1].w, hipModel[2].w);
    pose.knee = float3(kneeModel[0].w, kneeModel[1].w, kneeModel[2].w);
    pose.ankle = float3(ankleModel[0].w, ankleModel[1].w, ankleModel[2].w);

    // The foot's contact -- its lowest point once turned -- is dropped onto the ground and the
    // ankle carried by the same vector; see docs/skinning.md for why it is the contact and not the
    // sole's centre. Rigid with the ankle, since the foot keeps its orientation through the solve,
    // so the contact measured here is the contact that lands.
    //
    // Measured in model space through the ankle's transform, never in the stored ankle-local sole:
    // bone space carries whatever scale the bind does, so a distance read there is in those units.
    pose.weight = PlantWeight(rig, clip, index, frames);

    // The turn is the ground's slope, not whatever would bring the sole flat: a foot the animator
    // posed on its toe stays on its toe, and on level ground nothing turns.
    let floorUp = float3(0.0, 1.0, 0.0);
    float3 tilt = cross(floorUp, plane.xyz);
    pose.turnAxis = float3(0.0, 0.0, 1.0);
    pose.turnAngle = 0.0;
    if (length(tilt) > 1e-6)
    {
        pose.turnAxis = normalize(tilt);
        pose.turnAngle = min(acos(clamp(dot(floorUp, plane.xyz), -1.0, 1.0)), cSoleClampRadians) * pose.weight;
    }
    let turn = RotationAbout(float3(0.0, 0.0, 0.0), pose.turnAxis, pose.turnAngle);

    // The foot as it will stand, turned about the ankle, with the ankle at the origin.
    let sole = TransformDir(turn, TransformPoint(ankleModel, leg.solePoint.xyz) - pose.ankle);
    let soleUp = normalize(TransformDir(turn, TransformDir(ankleModel, leg.soleNormal.xyz)));
    let toeModel = PaletteMatrix(tables, paletteBase + leg.toe * cFloat4sPerBone);
    let toe = TransformDir(turn, float3(toeModel[0].w, toeModel[1].w, toeModel[2].w) - pose.ankle);

    // The heel and the ball: the two joints dropped onto the sole plane. Whichever is lower along
    // the ground normal is what touches.
    let heel = OnSolePlane(float3(0.0, 0.0, 0.0), sole, soleUp);
    let ball = OnSolePlane(toe, sole, soleUp);
    let contact = pose.ankle + (dot(heel, plane.xyz) < dot(ball, plane.xyz) ? heel : ball);

    // The same contact with no turn applied: where the clip itself put the foot down. The plant
    // preserves its height above the authored floor, so the turn's own lift is not read as ground.
    let soleFlat = TransformPoint(ankleModel, leg.solePoint.xyz) - pose.ankle;
    let soleUpFlat = normalize(TransformDir(ankleModel, leg.soleNormal.xyz));
    let toeFlat = float3(toeModel[0].w, toeModel[1].w, toeModel[2].w) - pose.ankle;
    let heelFlat = OnSolePlane(float3(0.0, 0.0, 0.0), soleFlat, soleUpFlat);
    let ballFlat = OnSolePlane(toeFlat, soleFlat, soleUpFlat);
    let authoredContact = pose.ankle + (dot(heelFlat, plane.xyz) < dot(ballFlat, plane.xyz) ? heelFlat : ballFlat);

    pose.target = lerp(pose.ankle, pose.ankle + GroundDeparture(plane, authored, down, contact, authoredContact), pose.weight);

    // How far the rig must come down, along the ground's up, for this chain to reach: the smaller
    // root of |v + d*n| = reach, where v is hip-to-target and n the ground normal.
    //
    // Not the straight-line shortfall `|v| - reach`, which is only correct when the target sits
    // directly below the hip: the drop is along n, and a target off to one side is closer to the
    // hip after dropping than that shortfall assumes, so the leg would still come up short on
    // exactly the sloped ground this feature exists for. One sqrt buys the exact answer.
    //
    // A negative discriminant is a target too far to one side for any drop to reach; the clamp then
    // yields -c, the drop that gets closest, and the solve stops short there rather than nowhere.
    let reach = length(pose.knee - pose.hip) + length(pose.ankle - pose.knee);
    let toTarget = pose.target - pose.hip;
    let c = dot(toTarget, plane.xyz);
    let disc = c * c - (dot(toTarget, toTarget) - reach * reach);

    // A chain that can already reach gives a non-positive root, which the clamp reads as no drop.
    pose.deficit = max(0.0, -c - sqrt(max(disc, 0.0)));

    return pose;
}

// Carries every descendant of a solved bone by that bone's delta, rigidly.
//
// Knows nothing about *why* a bone moved: it reads the `solved` entries some threads left in
// `gSolvedBone`/`gSolvedDelta` and applies each one to everything beneath it. So a second kind of
// solve -- a hand reaching for a prop -- calls this rather than copying it, which matters more than
// it looks: the barrier discipline above and the two exclusions below are the delicate part, and two
// copies of them are two things that can drift.
//
// Which delta a bone takes is its nearest solved ancestor's, so each walks up until it finds one --
// cut short at the shallowest solved depth, above which no ancestor can be one, which is what keeps
// a spine or a tail out of the walk. A bone that is itself solved is skipped: its own delta is
// already in its slot, and its parent's would be applied on top.
//
// @pre the caller has left a barrier between writing the table and calling this.
void CarrySolvedDescendants(PoseTables tables, Rig rig, uint solved, uint paletteBase, uint localIdx)
{
    for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = tables.bones.Get(rig.bones, bone);
        if (record.depth <= gShallowestSolved || SolvedIndexOf(solved, bone) >= 0)
        {
            continue;
        }

        uint ancestor = record.parent;
        int found = -1;
        while (ancestor != cInvalidBone)
        {
            SkinnedBone up = tables.bones.Get(rig.bones, ancestor);
            if (up.depth < gShallowestSolved)
            {
                break;
            }
            found = SolvedIndexOf(solved, ancestor);
            if (found >= 0)
            {
                break;
            }
            ancestor = up.parent;
        }

        if (found >= 0)
        {
            let slot = paletteBase + bone * cFloat4sPerBone;
            WritePalette(tables, slot, mul(gSolvedDelta[found], PaletteMatrix(tables, slot)));
        }
    }
    AllMemoryBarrierWithGroupSync();
}

// Plants every leg of one instance, in the model-space window described above.
//
// Group-uniform from end to end, which every barrier below depends on. Three of the four sit outside
// every branch; the fourth is inside `if (drop > 0.0)`, and that condition is read from groupshared
// after a barrier, so the whole group agrees on it. A branch on anything thread-varying must not
// carry one. One thread solves one leg -- a chain is serial, hip before knee before ankle, so there
// is nothing in it for a second lane to do -- and the whole group then carries the descendants.
void PlantFeet(PoseTables tables, Rig rig, Clip clip, float frames, float4x4 modelToWorld, uint paletteBase, uint localIdx)
{
    if (localIdx == 0)
    {
        gShallowestSolved = 0xffffffffu;
        gMaxDeficit = 0u;
    }
    AllMemoryBarrierWithGroupSync();

    // groundClips rests every clip's lowest point on model y = 0, so that plane is the floor the
    // animator posed against and the one a plant measures departure from.
    const float4 cAuthoredFloor = float4(0.0, 1.0, 0.0, 0.0);
    let plane = GroundPlaneInModel(gUniforms.groundNormal, gUniforms.groundPoint, modelToWorld);
    let down = normalize(TransformDir(AffineInverse(modelToWorld), float3(0.0, -1.0, 0.0)));

    SkinnedLegChain leg;
    if (localIdx < rig.legs.count)
    {
        leg = gUniforms.legBuffer.Get(rig.legs, localIdx);

        // Measured before anything moves: how far the worst leg falls short is what the whole rig
        // has to come down by, and that cannot be known one leg at a time.
        let reachOnly = ReadLeg(tables, rig, clip, leg, localIdx, frames, plane, cAuthoredFloor, down, paletteBase);
        InterlockedMax(gMaxDeficit, asuint(reachOnly.deficit));
    }
    AllMemoryBarrierWithGroupSync();

    // Lower the whole rig by the largest deficit, along the ground's own up. Group-uniform, so the
    // barrier inside is reached by the group or by none of it.
    //
    // The rig moves rather than the leg stretching because a leg that simply stops short snaps
    // straight, which reads as a limb popping; dropping the root is what a stride onto lower ground
    // actually does. Every bone moves, not just the ones below the hips: the rig is one body.
    let drop = asfloat(gMaxDeficit);
    if (drop > 0.0)
    {
        let offset = -drop * plane.xyz;
        for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
        {
            let slot = paletteBase + bone * cFloat4sPerBone;
            float4x4 lowered = PaletteMatrix(tables, slot);
            lowered[0].w += offset.x;
            lowered[1].w += offset.y;
            lowered[2].w += offset.z;
            WritePalette(tables, slot, lowered);
        }
        AllMemoryBarrierWithGroupSync();
    }

    if (localIdx < rig.legs.count)
    {
        // Re-read rather than reuse: every joint moved in the drop, and so did the ground under the
        // ankle, so a target measured before it is a target for a rig that no longer exists.
        let lowered = float4(0.0, 1.0, 0.0, drop * plane.y);
        let pose = ReadLeg(tables, rig, clip, leg, localIdx, frames, plane, lowered, down, paletteBase);

        let hipSlot = paletteBase + leg.hip * cFloat4sPerBone;
        let kneeSlot = paletteBase + leg.knee * cFloat4sPerBone;
        let ankleSlot = paletteBase + leg.ankle * cFloat4sPerBone;

        let hipModel = PaletteMatrix(tables, hipSlot);
        let kneeModel = PaletteMatrix(tables, kneeSlot);
        let ankleModel = PaletteMatrix(tables, ankleSlot);

        float4x4 hipDelta;
        float4x4 kneeDelta;
        SolveTwoBone(pose.hip, pose.knee, pose.ankle, pose.target, hipDelta, kneeDelta);

        // The foot keeps the orientation the clip gave it: the ankle is carried to where the solve
        // put the joint, in translation alone, so the shin's turn is absorbed at the ankle rather
        // than passed to the foot. Then it takes the turn ReadLeg measured the contact with, about
        // the joint -- not about the sole: the joint is where the chain the solve just satisfied
        // ends, and turning the foot under it would put the shin back out of length. The contact
        // was placed already turned, so it lands on the ground exactly.
        let jointNow = TransformPoint(kneeDelta, pose.ankle);
        float4x4 planted = ankleModel;
        planted[0].w += jointNow.x - pose.ankle.x;
        planted[1].w += jointNow.y - pose.ankle.y;
        planted[2].w += jointNow.z - pose.ankle.z;
        planted = mul(RotationAbout(jointNow, pose.turnAxis, pose.turnAngle), planted);

        WritePalette(tables, hipSlot, mul(hipDelta, hipModel));
        WritePalette(tables, kneeSlot, mul(kneeDelta, kneeModel));
        WritePalette(tables, ankleSlot, planted);

        let entry = localIdx * 3;
        gSolvedBone[entry + 0] = leg.hip;
        gSolvedBone[entry + 1] = leg.knee;
        gSolvedBone[entry + 2] = leg.ankle;
        gSolvedDelta[entry + 0] = hipDelta;
        gSolvedDelta[entry + 1] = kneeDelta;
        gSolvedDelta[entry + 2] = mul(planted, AffineInverse(ankleModel));

        InterlockedMin(gShallowestSolved, tables.bones.Get(rig.bones, leg.hip).depth);
    }
    AllMemoryBarrierWithGroupSync();

    CarrySolvedDescendants(tables, rig, rig.legs.count * 3, paletteBase, localIdx);
}

// Poses one instance into the palette starting at `paletteBase`, planting its feet in the one window
// of the walk where every slot holds a model-space transform.
//
// The plant is group-uniform -- a rig either authored legs or did not, and the scene's switch is
// one value -- so the barriers inside it are reached by the whole group or by none of it.
void PoseInstance(PoseTables tables, Rig rig, Clip clip, float frames, float4x4 modelToWorld, uint paletteBase, uint localIdx)
{
    PoseModelSpace(tables, rig, clip, frames, paletteBase, localIdx);

    if (!rig.legs.Null() && gUniforms.plantFeet != 0)
    {
        PlantFeet(tables, rig, clip, frames, modelToWorld, paletteBase, localIdx);
    }

    FoldInverseBind(tables, rig, paletteBase, localIdx);
}

[shader("compute")]
[numthreads(cPoseGroupSize, 1, 1)]
void main(uint groupIdx: SV_GroupID, uint localIdx: SV_GroupIndex)
{
    if (groupIdx >= gUniforms.posedCount)
    {
        return;
    }

    // Group-uniform, so the early-out above and every barrier below are reached by the whole group
    // or by none of it.
    let mesh = gUniforms.meshBuffer.Get(gUniforms.posedInstances[groupIdx]);
    SkinnedState state = gUniforms.playbackBuffer.LoadRecordAs<SkinnedState>(mesh.playback);
    Rig rig = gUniforms.rigs.Get(state.playback.rig);
    Clip clip = gUniforms.clipBuffer.Get(rig.clips, state.playback.clip);

    PoseTables tables;
    tables.bones = gUniforms.boneBuffer;
    tables.samples = gUniforms.sampleBuffer;
    tables.palettes = gUniforms.bonePalettes;

    let modelToWorld = mesh.Matrix();
    let stride = rig.boneCount * cFloat4sPerBone;
    let base = state.palette.GetStart();

    PoseInstance(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.time), modelToWorld, base, localIdx);
    PoseInstance(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.prevTime), modelToWorld, base + stride, localIdx);
}
