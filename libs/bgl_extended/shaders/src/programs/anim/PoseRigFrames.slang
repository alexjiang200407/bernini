import idl.BoneSample;
import idl.Clip;
import idl.Constants;
import idl.Rig;
import idl.SkinnedBone;
import lib.anim.pose_walk;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PoseTables;
import lib.types.RangeBuffer;

// Fills one rig's bone anim table: one workgroup per frame of its clip set, each running the same
// walk PoseSkinned runs per instance. Every instance of the rig then reads a pose instead of
// computing one, which is what makes the crowd tier cost nothing per unit.
//
// Dispatched per rig rather than over a list of them, because the group count is the rig's own frame
// count and a rig wanting its table is rare -- the first crowd instance on it, and a growth of the
// arena.

struct Uniforms
{
    EntryBuffer<Rig> rigs;

    RangeBuffer<SkinnedBone> boneBuffer;
    RangeBuffer<Clip> clipBuffer;
    RangeBuffer<BoneSample> sampleBuffer;

    ComputeBuffer<float4> boneAnimTables;

    // The rig being filled, by entry index rather than by Entry<Rig>: a uniform block carries
    // scalars and buffer handles, and the pass knows the index it is dispatching for.
    uint rigIndex;

    // Frames across every clip of the rig, which is the dispatch's group count.
    uint frameCount;
};

ConstantBuffer<Uniforms> gUniforms;

// The clip a global frame belongs to. Clips are laid out back to back in frame order, so this is a
// walk of a table a handful of entries long, run once per thread and outside the per-bone loop.
Clip ClipOfFrame(Rig rig, uint frame, out uint frameInClip)
{
    Clip found = gUniforms.clipBuffer.Get(rig.clips, 0);
    frameInClip = 0;

    for (uint i = 0; i < rig.clips.count; ++i)
    {
        Clip clip = gUniforms.clipBuffer.Get(rig.clips, i);
        if (frame >= clip.firstFrame && frame < clip.firstFrame + clip.frameCount)
        {
            found = clip;
            frameInClip = frame - clip.firstFrame;
        }
    }

    return found;
}

[shader("compute")]
[numthreads(cPoseGroupSize, 1, 1)]
void main(uint groupIdx: SV_GroupID, uint localIdx: SV_GroupIndex)
{
    if (groupIdx >= gUniforms.frameCount)
    {
        return;
    }

    // Group-uniform, so every barrier inside the walk is reached by the whole group or by none of it.
    Rig rig = gUniforms.rigs.Get(gUniforms.rigIndex);

    uint frameInClip;
    Clip clip = ClipOfFrame(rig, groupIdx, frameInClip);

    PoseTables tables;
    tables.bones = gUniforms.boneBuffer;
    tables.samples = gUniforms.sampleBuffer;
    tables.palettes = gUniforms.boneAnimTables;

    // Frame-major, exactly as the sample pool this is derived from: one frame is a contiguous run of
    // boneCount bones, so a reader indexes it with the same arithmetic.
    let base = rig.boneAnimTable.GetStart() + groupIdx * rig.boneCount * cFloat4sPerBone;

    // An integer frame, so the walk's blend weight is zero and it reads one sample per bone.
    PoseInto(tables, rig, clip, float(frameInClip), base, localIdx);
}
