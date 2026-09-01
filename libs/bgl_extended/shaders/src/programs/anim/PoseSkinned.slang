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
import lib.anim.pose_walk;
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
float4x4 InstanceTransform(MeshInstance mesh)
{
    return float4x4(mesh.transform[0], mesh.transform[1], mesh.transform[2], float4(0.0, 0.0, 0.0, 1.0));
}

// The inverse of an affine transform: the upper 3x3 inverted through its adjugate, and the
// translation carried back through that. Affine because an instance transform is -- the palette
// stores three rows and drops the fourth on the same assumption.
float4x4 AffineInverse(float4x4 m)
{
    float3x3 r = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    float3 t = float3(m[0].w, m[1].w, m[2].w);

    float3 c0 = cross(r[1], r[2]);
    float3 c1 = cross(r[2], r[0]);
    float3 c2 = cross(r[0], r[1]);

    float3x3 inv = transpose(float3x3(c0, c1, c2)) / dot(r[0], c0);
    float3 it = -mul(inv, t);

    return float4x4(float4(inv[0], it.x), float4(inv[1], it.y), float4(inv[2], it.z), float4(0.0, 0.0, 0.0, 1.0));
}

float3 TransformPoint(float4x4 m, float3 p)
{
    return mul(m, float4(p, 1.0)).xyz;
}

float3 TransformDir(float4x4 m, float3 v)
{
    return mul(m, float4(v, 0.0)).xyz;
}

// A rotation of `angle` about `axis` through `pivot`, as one affine matrix. Rodrigues rather than a
// quaternion: the result is multiplied into a model transform either way, so building the matrix
// directly saves the conversion.
float4x4 RotationAbout(float3 pivot, float3 axis, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    float t = 1.0 - c;

    float3 r0 = float3(t * axis.x * axis.x + c, t * axis.x * axis.y - s * axis.z, t * axis.x * axis.z + s * axis.y);
    float3 r1 = float3(t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + c, t * axis.y * axis.z - s * axis.x);
    float3 r2 = float3(t * axis.x * axis.z - s * axis.y, t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + c);

    // pivot - R * pivot, so the pivot is the one point the rotation leaves alone.
    float3 offset = pivot - float3(dot(r0, pivot), dot(r1, pivot), dot(r2, pivot));

    return float4x4(float4(r0, offset.x), float4(r1, offset.y), float4(r2, offset.z), float4(0.0, 0.0, 0.0, 1.0));
}

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

// The ground plane in the instance's model space, as `(normal, -distance)` with a unit normal.
//
// Carried as a plane rather than sampled in world space: a plane is a row vector, so it crosses a
// transform by multiplication from the left and needs no inverse of its own. The inverse is still
// wanted for the drop direction below, which is why both are built once per group.
float4 ModelGroundPlane(float4x4 modelToWorld)
{
    float4 world = float4(gUniforms.groundNormal, -dot(gUniforms.groundNormal, gUniforms.groundPoint));
    float4 model = mul(world, modelToWorld);
    return model / length(model.xyz);
}

// Where the ground sits under `point`, dropped along world down rather than along the plane's own
// normal: a planted foot must keep the ground position it was authored over, and the closest point
// on a slope is not that point.
//
// `down` is world -Y carried into model space. SetGround refuses a normal with no upward component,
// so the denominator here cannot be zero.
float3 SampleGround(float4 plane, float3 down, float3 point)
{
    let distance = dot(plane.xyz, point) + plane.w;
    return point + down * (distance / -dot(plane.xyz, down));
}

// The analytic two-bone solve: law of cosines on a root-mid-tip chain toward `target`, keeping the
// bend in the plane the chain is already bent in, then one aim rotation bringing the tip onto the
// target.
//
// Limb-neutral on purpose. A leg is what calls it here, but nothing below knows that: a shoulder,
// elbow and wrist reaching for a prop is the same three joints and the same maths, so whoever adds
// that reaches for this rather than writing a second copy of it. What a leg owns is where the
// target comes from and what weights it -- not the solve.
//
// `rootDelta` and `midDelta` come back as the model-space transforms to premultiply onto the root
// and onto everything from the mid joint down -- the tip included, since it is rigid below the mid.
void SolveTwoBone(float3 root, float3 mid, float3 tip, float3 target, out float4x4 rootDelta, out float4x4 midDelta)
{
    let upper = length(mid - root);
    let lower = length(tip - mid);

    // Held just inside full extension: at exactly upper + lower the bend plane is undefined, and one
    // frame of a straight chain reads as a snap.
    let reach = clamp(length(target - root), 1e-4, upper + lower - 1e-4);

    let toTip = normalize(tip - root);
    let toMid = normalize(mid - root);

    // The plane the chain is bent in. One that is already straight has no such plane, so nothing can
    // be said about which way it should bend and the solve leaves it alone.
    float3 axis = cross(toTip, toMid);
    if (length(axis) < 1e-6)
    {
        rootDelta = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
        midDelta = rootDelta;
        return;
    }
    axis = normalize(axis);

    let rootNow = acos(clamp(dot(toTip, toMid), -1.0, 1.0));
    let midNow = acos(clamp(dot(normalize(root - mid), normalize(tip - mid)), -1.0, 1.0));

    let rootWanted = acos(clamp((upper * upper + reach * reach - lower * lower) / (2.0 * upper * reach), -1.0, 1.0));
    let midWanted = acos(clamp((upper * upper + lower * lower - reach * reach) / (2.0 * upper * lower), -1.0, 1.0));

    let bendRoot = RotationAbout(root, axis, rootWanted - rootNow);
    let mid1 = TransformPoint(bendRoot, mid);
    let bendMid = RotationAbout(mid1, axis, midWanted - midNow);

    let tip1 = TransformPoint(bendMid, TransformPoint(bendRoot, tip));

    // Now the chain is the right shape but pointing where it was; swing it onto the target.
    let from = normalize(tip1 - root);
    let to = normalize(target - root);
    float3 aimAxis = cross(from, to);

    float4x4 aim = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
    if (length(aimAxis) > 1e-6)
    {
        aim = RotationAbout(root, normalize(aimAxis), acos(clamp(dot(from, to), -1.0, 1.0)));
    }

    rootDelta = mul(aim, bendRoot);
    midDelta = mul(aim, mul(bendMid, bendRoot));
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

    // The joint is aimed at the ground under it lifted by the foot's own height, rather than at
    // wherever the sole currently is: the solve rotates the ankle along with the shin, so a target
    // derived from the sole's present position would be a target the solve then moves. Taken with
    // the tilt -- which brings the sole normal onto the ground's -- the sole lands exactly on the
    // plane whatever the chain did, since the only part of the offset that has height is the part
    // along that normal.
    pose.weight = PlantWeight(rig, clip, index, frames);
    let footHeight = -dot(leg.solePoint.xyz, normalize(leg.soleNormal.xyz));
    pose.target = lerp(pose.ankle, SampleGround(plane, down, pose.ankle) + plane.xyz * footHeight, pose.weight);

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

    let plane = ModelGroundPlane(modelToWorld);
    let down = normalize(TransformDir(AffineInverse(modelToWorld), float3(0.0, -1.0, 0.0)));

    SkinnedLegChain leg;
    if (localIdx < rig.legs.count)
    {
        leg = gUniforms.legBuffer.Get(rig.legs, localIdx);

        // Measured before anything moves: how far the worst leg falls short is what the whole rig
        // has to come down by, and that cannot be known one leg at a time.
        let reachOnly = ReadLeg(tables, rig, clip, leg, localIdx, frames, plane, down, paletteBase);
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
        let pose = ReadLeg(tables, rig, clip, leg, localIdx, frames, plane, down, paletteBase);

        let hipSlot = paletteBase + leg.hip * cFloat4sPerBone;
        let kneeSlot = paletteBase + leg.knee * cFloat4sPerBone;
        let ankleSlot = paletteBase + leg.ankle * cFloat4sPerBone;

        let hipModel = PaletteMatrix(tables, hipSlot);
        let kneeModel = PaletteMatrix(tables, kneeSlot);
        let ankleModel = PaletteMatrix(tables, ankleSlot);

        float4x4 hipDelta;
        float4x4 kneeDelta;
        SolveTwoBone(pose.hip, pose.knee, pose.ankle, pose.target, hipDelta, kneeDelta);

        // Rotated about the ankle joint, not about the sole: the joint is where the chain the solve
        // just satisfied ends, and turning the foot under it would put the shin back out of length.
        // The sole then lifts off the plane by (1 - cos t) of its distance to the joint, which at
        // the 30 degree clamp is under two millimetres on a foot of any size we cook.
        float4x4 planted = mul(kneeDelta, ankleModel);
        let soleNormal = normalize(TransformDir(planted, leg.soleNormal.xyz));
        float3 tilt = cross(soleNormal, plane.xyz);
        if (length(tilt) > 1e-6)
        {
            let angle = acos(clamp(dot(soleNormal, plane.xyz), -1.0, 1.0));
            let joint = float3(planted[0].w, planted[1].w, planted[2].w);
            planted = mul(RotationAbout(joint, normalize(tilt), min(angle, cSoleClampRadians) * pose.weight), planted);
        }

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
// The plant is group-uniform -- a rig either authored legs or did not -- so the barriers inside it
// are reached by the whole group or by none of it.
void PoseInstance(PoseTables tables, Rig rig, Clip clip, float frames, float4x4 modelToWorld, uint paletteBase, uint localIdx)
{
    PoseModelSpace(tables, rig, clip, frames, paletteBase, localIdx);

    if (!rig.legs.Null())
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

    let modelToWorld = InstanceTransform(mesh);
    let stride = rig.boneCount * cFloat4sPerBone;
    let base = state.palette.GetStart();

    PoseInstance(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.time), modelToWorld, base, localIdx);
    PoseInstance(tables, rig, clip, ClipFrames(clip, state.playback, gUniforms.prevTime), modelToWorld, base + stride, localIdx);
}
