import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPlayback;
import idl.Mesh;
import idl.RawEntry;
import idl.SkinnedBone;
import idl.SkinnedGeom;
import idl.SkinnedLegChain;
import idl.SkinnedState;
import lib.anim.clip_playback;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.RangeBuffer;
import lib.types.RawBuffer;
import lib.types.UploadBuffer;

// One workgroup poses one skinned instance: sample its clip, walk the hierarchy, multiply through the
// inverse bind, write the palette. Twice over -- once at `time` and once at `prevTime` -- so the mesh
// shader can skin both and derive a motion vector without a history buffer.

struct Uniforms
{
    // The mesh instances to pose, one per workgroup. Dense: erasing a placement only releases its
    // bytes, so a sweep of the playback arena would pose freed instances -- and would meet the VAT
    // records sharing it, which this pass has nothing to do with.
    UploadBuffer<uint> posedInstances;

    // Where an instance stands and which record it plays from. Both are read from the placement
    // rather than copied into the playback record: a foot is planted against the ground under the
    // instance in the world, and one transform in two records is one that can disagree with itself.
    EntryBuffer<Mesh> meshBuffer;

    RawBuffer playbackBuffer;
    EntryBuffer<SkinnedGeom> skinnedGeoms;

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

// One bone's transform, read back out of the three rows a palette slot holds. Affine, so the fourth
// row is (0,0,0,1) and was never stored -- the same convention the mesh shader reads.
float4x4 PaletteMatrix(uint slot)
{
    return float4x4(
        gUniforms.bonePalettes[slot + 0], gUniforms.bonePalettes[slot + 1], gUniforms.bonePalettes[slot + 2], float4(0.0, 0.0, 0.0, 1.0)
    );
}

void WritePalette(uint slot, float4x4 transform)
{
    gUniforms.bonePalettes[slot + 0] = transform[0];
    gUniforms.bonePalettes[slot + 1] = transform[1];
    gUniforms.bonePalettes[slot + 2] = transform[2];
}

// A local TRS as the matrix mul() expects: rows of the rotation scaled per axis, translation in .w.
float4x4 LocalTransform(BoneSample sample)
{
    float4 q = normalize(sample.rotation);
    float3 s = sample.scale.xyz;

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    float3 r0 = float3(1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy));
    float3 r1 = float3(2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx));
    float3 r2 = float3(2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy));

    return float4x4(
        float4(r0 * s, sample.translation.x),
        float4(r1 * s, sample.translation.y),
        float4(r2 * s, sample.translation.z),
        float4(0.0, 0.0, 0.0, 1.0)
    );
}

// Blended local pose of bone `bone` at a fractional frame. Rotation is nlerp'd with a hemisphere
// flip, not slerp: clips are resampled to a fixed rate, so the angle between adjacent frames is small
// enough that the error is invisible and the transcendentals are not worth paying per bone.
BoneSample SampleBone(SkinnedGeom geom, Clip clip, uint bone, float frames)
{
    uint f0;
    uint f1;
    float weight;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    let base = clip.firstFrame * geom.boneCount + bone;
    BoneSample a = gUniforms.sampleBuffer.Get(geom.samples, base + f0 * geom.boneCount);
    BoneSample b = gUniforms.sampleBuffer.Get(geom.samples, base + f1 * geom.boneCount);

    // Without the flip a pair straddling the hemisphere boundary interpolates the long way round,
    // which reads as the bone snapping through its own axis.
    float4 rotB = (dot(a.rotation, b.rotation) < 0.0) ? -b.rotation : b.rotation;

    BoneSample blended;
    blended.translation = lerp(a.translation, b.translation, weight);
    blended.rotation = normalize(lerp(a.rotation, rotB, weight));
    blended.scale = lerp(a.scale, b.scale, weight);
    return blended;
}

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
float PlantWeight(SkinnedGeom geom, Clip clip, uint leg, float frames)
{
    uint f0;
    uint f1;
    float blend;
    ClipFrameIndices(clip, frames, f0, f1, blend);

    let legs = geom.legs.count;
    let a = (clip.firstFrame + f0) * legs + leg;
    let b = (clip.firstFrame + f1) * legs + leg;

    let wordA = gUniforms.plantWeightBuffer.Get(geom.plantWeights, a / 4);
    let wordB = gUniforms.plantWeightBuffer.Get(geom.plantWeights, b / 4);

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
void CarrySolvedDescendants(SkinnedGeom geom, uint solved, uint paletteBase, uint localIdx)
{
    for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);
        if (record.depth <= gShallowestSolved || SolvedIndexOf(solved, bone) >= 0)
        {
            continue;
        }

        uint ancestor = record.parent;
        int found = -1;
        while (ancestor != cInvalidBone)
        {
            SkinnedBone up = gUniforms.boneBuffer.Get(geom.bones, ancestor);
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
            WritePalette(slot, mul(gSolvedDelta[found], PaletteMatrix(slot)));
        }
    }
    AllMemoryBarrierWithGroupSync();
}

// Plants every leg of one instance, in the model-space window described above.
//
// Group-uniform from end to end: the early-out is on `geom`, which the whole group shares, and both
// barriers sit outside the per-leg and per-bone branches. One thread solves one leg -- a chain is
// serial, hip before knee before ankle, so there is nothing in it for a second lane to do -- and the
// whole group then carries the descendants.
void PlantFeet(SkinnedGeom geom, Clip clip, float frames, float4x4 modelToWorld, uint paletteBase, uint localIdx)
{
    if (localIdx == 0)
    {
        gShallowestSolved = 0xffffffffu;
    }
    AllMemoryBarrierWithGroupSync();

    let plane = ModelGroundPlane(modelToWorld);
    let down = normalize(TransformDir(AffineInverse(modelToWorld), float3(0.0, -1.0, 0.0)));

    if (localIdx < geom.legs.count)
    {
        SkinnedLegChain leg = gUniforms.legBuffer.Get(geom.legs, localIdx);

        let hipSlot = paletteBase + leg.hip * cFloat4sPerBone;
        let kneeSlot = paletteBase + leg.knee * cFloat4sPerBone;
        let ankleSlot = paletteBase + leg.ankle * cFloat4sPerBone;

        let hipModel = PaletteMatrix(hipSlot);
        let kneeModel = PaletteMatrix(kneeSlot);
        let ankleModel = PaletteMatrix(ankleSlot);

        let hip = float3(hipModel[0].w, hipModel[1].w, hipModel[2].w);
        let knee = float3(kneeModel[0].w, kneeModel[1].w, kneeModel[2].w);
        let ankle = float3(ankleModel[0].w, ankleModel[1].w, ankleModel[2].w);

        // The joint is aimed at the ground under it lifted by the foot's own height, rather than at
        // wherever the sole currently is: the solve rotates the ankle along with the shin, so a
        // target derived from the sole's present position would be a target the solve then moves.
        // Taken with the tilt below -- which brings the sole normal onto the ground's -- the sole
        // lands exactly on the plane whatever the chain did, since the only part of the offset that
        // has height is the part along that normal.
        let weight = PlantWeight(geom, clip, localIdx, frames);
        let footHeight = -dot(leg.solePoint.xyz, normalize(leg.soleNormal.xyz));
        let target = lerp(ankle, SampleGround(plane, down, ankle) + plane.xyz * footHeight, weight);

        float4x4 hipDelta;
        float4x4 kneeDelta;
        SolveTwoBone(hip, knee, ankle, target, hipDelta, kneeDelta);

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
            planted = mul(RotationAbout(joint, normalize(tilt), min(angle, cSoleClampRadians) * weight), planted);
        }

        WritePalette(hipSlot, mul(hipDelta, hipModel));
        WritePalette(kneeSlot, mul(kneeDelta, kneeModel));
        WritePalette(ankleSlot, planted);

        let entry = localIdx * 3;
        gSolvedBone[entry + 0] = leg.hip;
        gSolvedBone[entry + 1] = leg.knee;
        gSolvedBone[entry + 2] = leg.ankle;
        gSolvedDelta[entry + 0] = hipDelta;
        gSolvedDelta[entry + 1] = kneeDelta;
        gSolvedDelta[entry + 2] = mul(planted, AffineInverse(ankleModel));

        InterlockedMin(gShallowestSolved, gUniforms.boneBuffer.Get(geom.bones, leg.hip).depth);
    }
    AllMemoryBarrierWithGroupSync();

    CarrySolvedDescendants(geom, geom.legs.count * 3, paletteBase, localIdx);
}

// Poses one instance into the palette starting at `paletteBase`.
//
// Every barrier below must be reached by every thread of the group the same number of times. It is:
// the only loop carrying one is bounded by `geom.maxDepth`, which is group-uniform (the whole group
// poses one instance, so it reads one geom), and the strided per-bone loops that *are* thread-varying
// contain no barrier. Nothing returns early inside here either.
void PoseInto(SkinnedGeom geom, Clip clip, SkinnedState state, float4x4 modelToWorld, float time, uint paletteBase, uint localIdx)
{
    let frames = ClipFrames(clip, state.phase, state.rate, time);

    // Composed in the palette rather than in groupshared: a model transform is affine, so it fits the
    // three rows the slot already reserves for the skin matrix it ends up holding. Nothing bounds the
    // bone count then -- the palette is sized per rig, and 12 KiB of groupshared was what stopped a
    // character rig at 192.
    for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
    {
        WritePalette(paletteBase + bone * cFloat4sPerBone, LocalTransform(SampleBone(geom, clip, bone, frames)));
    }
    DeviceMemoryBarrierWithGroupSync();

    // One level at a time: a bone's parent is always a lower index, but a lower index is not
    // necessarily a shallower one, so composing in index order would read a parent this pass has not
    // composed yet. The bound is group-uniform, so every thread runs every level -- including a
    // thread holding no bone at that depth, which still has to arrive at the barrier.
    //
    // The barrier orders device memory, not groupshared: what one thread writes here another reads
    // at the next level, through the palette.
    for (uint level = 1; level <= geom.maxDepth; ++level)
    {
        for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
        {
            SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);
            if (record.depth == level)
            {
                let slot = paletteBase + bone * cFloat4sPerBone;
                WritePalette(slot, mul(PaletteMatrix(paletteBase + record.parent * cFloat4sPerBone), PaletteMatrix(slot)));
            }
        }
        DeviceMemoryBarrierWithGroupSync();
    }

    // The one window in the frame where every slot holds a model-space transform, which is what a
    // foot planted against a world ground plane has to be solved in. Group-uniform, so the barriers
    // inside are reached by the whole group or by none of it.
    if (!geom.legs.Null())
    {
        PlantFeet(geom, clip, frames, modelToWorld, paletteBase, localIdx);
    }

    // In place, and safe without a barrier between the two: the strided loops assign a bone to the
    // same thread every time, so the slot read here is the one this thread composed above.
    for (uint bone = localIdx; bone < geom.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = gUniforms.boneBuffer.Get(geom.bones, bone);
        let slot = paletteBase + bone * cFloat4sPerBone;
        WritePalette(slot, mul(PaletteMatrix(slot), record.inverseBind));
    }
    DeviceMemoryBarrierWithGroupSync();
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
    SkinnedGeom geom = gUniforms.skinnedGeoms.Get(state.geom);
    Clip clip = gUniforms.clipBuffer.Get(geom.clips, state.clip);

    let stride = geom.boneCount * cFloat4sPerBone;

    let base = state.palette.GetStart();

    PoseInto(geom, clip, state, mesh.transform, gUniforms.time, base, localIdx);
    PoseInto(geom, clip, state, mesh.transform, gUniforms.prevTime, base + stride, localIdx);
}
