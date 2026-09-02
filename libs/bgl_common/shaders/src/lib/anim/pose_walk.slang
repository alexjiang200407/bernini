import lib.anim.clip_playback;
import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPoseTables;
import idl.Rig;
import idl.SkinnedBone;

// The pose walk both animated kernels run: sample a clip at a fractional frame, compose the
// hierarchy one depth level at a time, and multiply each bone through its inverse bind. PoseSkinned
// runs it per instance per frame; PoseRigFrames runs it once per frame of a rig's clip set and
// never again.

// One bone's transform, read back out of the three rows a palette slot holds. Affine, so the fourth
// row is (0,0,0,1) and was never stored -- the same convention the mesh shader reads.
public float4x4 PaletteMatrix<T : IPoseTables>(T tables, uint slot)
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

// Poses one rig at one fractional frame into the slots starting at `paletteBase`.
//
// Every barrier below must be reached by every thread of the group the same number of times. It is:
// the only loop carrying one is bounded by `rig.maxDepth`, which is group-uniform (the whole group
// poses one rig at one frame), and the strided per-bone loops that *are* thread-varying
// contain no barrier. Nothing returns early inside here either.
public void PoseInto<T : IPoseTables>(T tables, Rig rig, Clip clip, float frames, uint paletteBase, uint localIdx)
{
    // Composed in the palette rather than in groupshared: a model transform is affine, so it fits the
    // three rows the slot already reserves for the skin matrix it ends up holding. Nothing bounds the
    // bone count then -- the palette is sized per rig, and 12 KiB of groupshared was what stopped a
    // character rig at 192.
    for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
    {
        WritePalette(tables, paletteBase + bone * cFloat4sPerBone, LocalTransform(SampleBone(tables, rig, clip, bone, frames)));
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

    // In place, and safe without a barrier between the two: the strided loops assign a bone to the
    // same thread every time, so the slot read here is the one this thread composed above.
    for (uint bone = localIdx; bone < rig.boneCount; bone += cPoseGroupSize)
    {
        SkinnedBone record = tables.GetBone(rig.bones, bone);
        let slot = paletteBase + bone * cFloat4sPerBone;
        WritePalette(tables, slot, mul(PaletteMatrix(tables, slot), record.inverseBind));
    }
    DeviceMemoryBarrierWithGroupSync();
}
