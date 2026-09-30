import lib.anim.clip_playback;
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IBonePalette;
import idl.IPoseTables;
import idl.Rig;
import idl.SkinnedBone;

// The pose walk both animated kernels run: sample the clips a local pose blends, compose the
// hierarchy one depth level at a time, and multiply each bone through its inverse bind. PoseSkinned
// runs it per instance per frame over that instance's weighted slots; PoseRigFrames runs it once per
// frame of a rig's clip set, one clip at a time, and never again.

// One bone's transform, read back out of the three rows a palette slot holds. Affine, so the fourth
// row is (0,0,0,1) and was never stored. The vertex blend in skinning.slang reads through this too.
public float4x4 PaletteMatrix<T : IBonePalette>(T tables, uint slot)
{
    return float4x4(tables.GetPalette(slot + 0), tables.GetPalette(slot + 1), tables.GetPalette(slot + 2), float4(0.0, 0.0, 0.0, 1.0));
}

public void WritePalette<T : IPoseTables>(T tables, uint slot, float4x4 transform)
{
    tables.SetPalette(slot + 0, transform[0]);
    tables.SetPalette(slot + 1, transform[1]);
    tables.SetPalette(slot + 2, transform[2]);
}

// A local TRS as the matrix mul() expects: rows of the rotation scaled per axis, translation in .w.
public float4x4 LocalTransform(BoneSample sample)
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
public BoneSample SampleBone<T : IPoseTables>(T tables, Rig rig, Clip clip, uint bone, float frames)
{
    uint f0;
    uint f1;
    float weight;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    let base = clip.firstFrame * rig.boneCount + bone;
    BoneSample a = tables.GetSample(rig.samples, base + f0 * rig.boneCount);
    BoneSample b = tables.GetSample(rig.samples, base + f1 * rig.boneCount);

    // Without the flip a pair straddling the hemisphere boundary interpolates the long way round,
    // which reads as the bone snapping through its own axis.
    float4 rotB = (dot(a.rotation, b.rotation) < 0.0) ? -b.rotation : b.rotation;

    BoneSample blended;
    blended.translation = lerp(a.translation, b.translation, weight);
    blended.rotation = normalize(lerp(a.rotation, rotB, weight));
    blended.scale = lerp(a.scale, b.scale, weight);
    return blended;
}

// The local pose a walk starts from: up to cMaxPoseClips weighted clip samples, the live ones packed
// first and their weights summing to one. Group-uniform -- every thread of a group resolves the same
// record at the same time -- so a loop over it carries no barrier hazard.
public struct LocalPose
{
    public Clip clips[cMaxPoseClips];
    public float frames[cMaxPoseClips];
    public float weights[cMaxPoseClips];
    public uint count;
};

// No live slot yet: what a caller fills before packing what it resolved.
public LocalPose EmptyPose()
{
    LocalPose pose;
    for (uint s = 0; s < cMaxPoseClips; ++s)
    {
        pose.clips[s] = Clip();
        pose.frames[s] = 0.0;
        pose.weights[s] = 0.0;
    }
    pose.count = 0;
    return pose;
}

public LocalPose SingleClipPose(Clip clip, float frames)
{
    LocalPose pose = EmptyPose();
    pose.clips[0] = clip;
    pose.frames[0] = frames;
    pose.weights[0] = 1.0;
    pose.count = 1;
    return pose;
}

// One bone's local pose under `pose`. A single clip is SampleBone and nothing else, so the crowd
// table and a plain hero instance keep the bytes they had. Several blend as assetlib's weighted
// poseModelTransforms does -- the reference this is diffed against: translation and scale by the
// weighted sum, each rotation flipped against the sum so far and then the sum normalized.
public BoneSample SampleLocal<T : IPoseTables>(T tables, Rig rig, LocalPose pose, uint bone)
{
    if (pose.count == 1)
    {
        return SampleBone(tables, rig, pose.clips[0], bone, pose.frames[0]);
    }

    BoneSample blended;
    blended.translation = float4(0.0);
    blended.rotation = float4(0.0);
    blended.scale = float4(0.0);

    for (uint s = 0; s < pose.count; ++s)
    {
        BoneSample local = SampleBone(tables, rig, pose.clips[s], bone, pose.frames[s]);
        float weight = pose.weights[s];

        blended.translation += weight * local.translation;
        blended.scale += weight * local.scale;

        // Against the running sum rather than a fixed reference, so the sign decision is continuous
        // in the weights: a crossfade passing 50 % flips nothing.
        float4 aligned = (dot(blended.rotation, local.rotation) < 0.0) ? -local.rotation : local.rotation;
        blended.rotation += weight * aligned;
    }

    blended.rotation = normalize(blended.rotation);
    return blended;
}

// Composes one rig under one local pose into the slots starting at `paletteBase`, leaving every
// slot holding a *model-space* transform.
//
// Split from the inverse bind below rather than run with it, because between the two is the one
// window in the frame where a bone is in model space -- which is where a foot is planted against a
// world ground plane. A caller wanting nothing of that calls PoseInto and never sees the seam.
//
// Every barrier below must be reached by every thread of the group the same number of times. It is:
// the only loop carrying one is bounded by `rig.maxDepth`, which is group-uniform (the whole group
// poses one rig at one frame), and the strided per-bone loops that *are* thread-varying
// contain no barrier. Nothing returns early inside here either.
public void PoseModelSpace<T : IPoseTables>(T tables, Rig rig, LocalPose pose, uint paletteBase, uint localIdx)
{
    // Composed in the palette rather than in groupshared: a model transform is affine, so it fits the
    // three rows the slot already reserves for the skin matrix it ends up holding. Nothing bounds the
    // bone count then -- the palette is sized per rig, and 12 KiB of groupshared was what stopped a
    // character rig at 192.
    for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
    {
        WritePalette(tables, paletteBase + bone * cFloat4sPerBone, LocalTransform(SampleLocal(tables, rig, pose, bone)));
    }
    DeviceMemoryBarrierWithGroupSync();

    // One level at a time: a bone's parent is always a lower index, but a lower index is not
    // necessarily a shallower one, so composing in index order would read a parent this pass has not
    // composed yet. The bound is group-uniform, so every thread runs every level -- including a
    // thread holding no bone at that depth, which still has to arrive at the barrier.
    //
    // The barrier orders device memory, not groupshared: what one thread writes here another reads
    // at the next level, through the palette.
    for (uint level = 1; level <= rig.maxDepth; ++level)
    {
        for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
        {
            SkinnedBone record = tables.GetBone(rig.bones, bone);
            if (record.depth == level)
            {
                let slot = paletteBase + bone * cFloat4sPerBone;
                WritePalette(
                    tables, slot, mul(PaletteMatrix(tables, paletteBase + record.parent * cFloat4sPerBone), PaletteMatrix(tables, slot))
                );
            }
        }
        DeviceMemoryBarrierWithGroupSync();
    }
}

// Folds each bone's inverse bind into the model transform PoseModelSpace left in its slot, turning
// the palette into the skinning matrices the mesh shader reads.
//
// In place, and safe without a barrier before it: the strided loops assign a bone to the same thread
// every time, so the slot read here is the one this thread composed.
public void FoldInverseBind<T : IPoseTables>(T tables, Rig rig, uint paletteBase, uint localIdx)
{
    for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = tables.GetBone(rig.bones, bone);
        let slot = paletteBase + bone * cFloat4sPerBone;
        WritePalette(tables, slot, mul(PaletteMatrix(tables, slot), record.inverseBind));
    }
    DeviceMemoryBarrierWithGroupSync();
}

// The whole walk, for a caller with nothing to do in the model-space window.
public void PoseInto<T : IPoseTables>(T tables, Rig rig, LocalPose pose, uint paletteBase, uint localIdx)
{
    PoseModelSpace(tables, rig, pose, paletteBase, localIdx);
    FoldInverseBind(tables, rig, paletteBase, localIdx);
}
