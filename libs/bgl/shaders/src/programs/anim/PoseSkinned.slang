import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.IPlayback;
import idl.Mesh;
import idl.RawEntry;
import idl.SkinnedBone;
import idl.SkinnedGeom;
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

// Poses one instance into the palette starting at `paletteBase`.
//
// Every barrier below must be reached by every thread of the group the same number of times. It is:
// the only loop carrying one is bounded by `geom.maxDepth`, which is group-uniform (the whole group
// poses one instance, so it reads one geom), and the strided per-bone loops that *are* thread-varying
// contain no barrier. Nothing returns early inside here either.
void PoseInto(SkinnedGeom geom, Clip clip, SkinnedState state, float time, uint paletteBase, uint localIdx)
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

    PoseInto(geom, clip, state, gUniforms.time, base, localIdx);
    PoseInto(geom, clip, state, gUniforms.prevTime, base + stride, localIdx);
}
