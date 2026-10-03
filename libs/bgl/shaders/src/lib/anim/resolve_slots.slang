// A playback record's weighted slots resolved at one clock: each live slot's clip, or a blend
// space's two members, at its frame position and normalized weight. PoseSkinned blends what this
// returns; every reader of an automatic instance's table pose takes its heaviest entry, so the clip
// the table draws is the one the blend leans on most.
//
// Values and loads only, through the tables the caller hands it. Group-uniform wherever it runs:
// one record at one time.

import idl.BlendNode;
import idl.BlendNodeKind;
import idl.BlendSlot;
import idl.BlendSpaceSample;
import idl.Clip;
import idl.Constants;
import idl.Rig;
import lib.anim.blend_slots;
import lib.anim.blend_space;
import lib.anim.clip_playback;
import lib.anim.pose_walk;
import lib.types.RangeBuffer;

// The rig tables a resolve reads: its node table, its clips, and the samples its spaces address.
public struct BlendTables
{
    public RangeBuffer<BlendNode> nodes;
    public RangeBuffer<Clip> clips;
    public RangeBuffer<BlendSpaceSample> samples;
};

// The clip a space's sample plays, and the seconds its cycle spans.
float SampleCycleSeconds(BlendTables tables, Rig rig, uint sample)
{
    let entry = tables.samples.Get(rig.blendSamples, sample);
    return SampleSeconds(tables.clips.Get(rig.clips, entry.clip));
}

// The two samples of `node` straddling `parameter`, and how far between them it sits. A parameter
// outside the authored range clamps onto the end sample, which then plays alone.
void SpaceSamples(BlendTables tables, Rig rig, BlendNode node, float parameter, out uint lo, out uint hi, out float weight)
{
    let last = node.firstSample + node.sampleCount - 1;

    lo = node.firstSample;
    hi = node.firstSample;
    weight = 0.0;

    if (parameter >= tables.samples.Get(rig.blendSamples, last).parameter)
    {
        lo = last;
        hi = last;
        return;
    }

    // A walk, not a search: a space is a handful of samples, and this runs once per slot per group
    // rather than per bone.
    for (uint i = node.firstSample; i < last; ++i)
    {
        let b = tables.samples.Get(rig.blendSamples, i + 1).parameter;
        if (parameter < b)
        {
            let a = tables.samples.Get(rig.blendSamples, i).parameter;
            lo = i;
            hi = i + 1;
            weight = SpanWeight(a, b, parameter);
            return;
        }
    }
}

// The weighted cycle length in seconds at `parameter` -- the `D` the phase integral divides by.
float SpaceSeconds(BlendTables tables, Rig rig, BlendNode node, float parameter)
{
    uint lo;
    uint hi;
    float weight;
    SpaceSamples(tables, rig, node, parameter, lo, hi, weight);

    return lerp(SampleCycleSeconds(tables, rig, lo), SampleCycleSeconds(tables, rig, hi), weight);
}

// The slot's parameter at `time`, on the same ramp shape its weight uses.
public float SlotParameter(BlendSlot slot, float time)
{
    if (time < slot.paramStart)
    {
        return slot.param0;
    }
    if (time >= slot.paramEnd)
    {
        return slot.param1;
    }
    return lerp(slot.param0, slot.param1, (time - slot.paramStart) / (slot.paramEnd - slot.paramStart));
}

// The space's shared normalized phase at `time`: `phase` advanced from `tRef` at `rate` cycles per
// weighted cycle length, integrated in closed form because the length moves while the parameter
// ramps.
//
// Split at the ramp's ends and at every sample the parameter crosses, because `D` is only linear in
// the parameter *between* two adjacent samples and kinks at each one. At most `sampleCount + 1`
// terms, once per slot per group.
float SpacePhase(BlendTables tables, Rig rig, BlendNode node, BlendSlot slot, float time)
{
    // Nothing to integrate over, and the case a rewrite rebased to now hits at prevTime.
    if (time <= slot.tRef)
    {
        return slot.phase;
    }

    float elapsed = 0.0;

    // Before the ramp the parameter is fixed, so the cycle is too.
    let rampFrom = clamp(slot.paramStart, slot.tRef, time);
    if (rampFrom > slot.tRef)
    {
        elapsed += (rampFrom - slot.tRef) / SpaceSeconds(tables, rig, node, slot.param0);
    }

    // Through the ramp, one term per span the parameter crosses.
    let rampTo = clamp(slot.paramEnd, rampFrom, time);
    if (rampTo > rampFrom)
    {
        let pFrom = SlotParameter(slot, rampFrom);
        let pTo = SlotParameter(slot, rampTo);

        float tSegment = rampFrom;
        float pSegment = pFrom;
        float dSegment = SpaceSeconds(tables, rig, node, pFrom);

        // Walked in the direction the parameter moves, not in table order: the segments have to be
        // accumulated in the order they are reached, or a falling ramp fuses two spans of a kinked
        // `D` into one closed-form term and the integral is wrong. Bounded by the table, not by the
        // ramp, and the bound is group-uniform.
        let ascending = pTo > pFrom;
        for (uint k = 0; k < node.sampleCount; ++k)
        {
            let i = ascending ? (node.firstSample + k) : (node.firstSample + node.sampleCount - 1 - k);

            let edge = tables.samples.Get(rig.blendSamples, i).parameter;
            if (!(ascending ? (edge > pSegment && edge < pTo) : (edge < pSegment && edge > pTo)))
            {
                continue;
            }

            // Where the ramp reaches this sample, by linear interpolation of the parameter in time.
            let tEdge = lerp(rampFrom, rampTo, (edge - pFrom) / (pTo - pFrom));
            let dEdge = SpaceSeconds(tables, rig, node, edge);

            elapsed += PhaseElapsed(tEdge - tSegment, dSegment, dEdge);

            tSegment = tEdge;
            pSegment = edge;
            dSegment = dEdge;
        }

        elapsed += PhaseElapsed(rampTo - tSegment, dSegment, SpaceSeconds(tables, rig, node, pTo));
    }

    // After the ramp the parameter is fixed again.
    if (time > rampTo)
    {
        elapsed += (time - rampTo) / SpaceSeconds(tables, rig, node, slot.param1);
    }

    return frac(slot.phase + slot.rate * elapsed);
}

// The record's slots at `time`: every clip carrying weight, its frame position, weights normalized
// to one. Group-uniform, since the whole group reads one record at one time.
//
// A clip node contributes one clip; a space node contributes the two samples straddling its
// parameter, which is why the pose holds twice the slots.
public LocalPose ResolveSlots(BlendTables tables, BlendSlot slots[cBlendSlots], Rig rig, float time)
{
    LocalPose pose = EmptyPose();

    float total = 0.0;
    for (uint s = 0; s < cBlendSlots; ++s)
    {
        BlendSlot slot = slots[s];
        float weight = SlotWeight(slot, time);
        if (weight <= 0.0)
        {
            continue;
        }

        BlendNode node = tables.nodes.Get(rig.nodes, slot.nodeIndex);

        if (node.kind == BlendNodeKind.kClip)
        {
            Clip clip = tables.clips.Get(rig.clips, node.clip);

            pose.clips[pose.count] = clip;
            pose.frames[pose.count] = SlotFrames(clip, slot, time);
            pose.weights[pose.count] = weight;
            total += weight;
            ++pose.count;
            continue;
        }

        let parameter = SlotParameter(slot, time);
        let phase = SpacePhase(tables, rig, node, slot, time);

        uint lo;
        uint hi;
        float between;
        SpaceSamples(tables, rig, node, parameter, lo, hi, between);

        // The phase is normalized and shared; each sample's frame is that fraction of its own
        // cycle, which is what keeps clips of different lengths in step.
        let loEntry = tables.samples.Get(rig.blendSamples, lo);
        let hiEntry = tables.samples.Get(rig.blendSamples, hi);
        let loClip = tables.clips.Get(rig.clips, loEntry.clip);
        let hiClip = tables.clips.Get(rig.clips, hiEntry.clip);

        if (between < 1.0)
        {
            pose.clips[pose.count] = loClip;
            pose.frames[pose.count] = phase * ClipCycle(loClip);
            pose.weights[pose.count] = weight * (1.0 - between);
            total += pose.weights[pose.count];
            ++pose.count;
        }
        if (between > 0.0 && hi != lo)
        {
            pose.clips[pose.count] = hiClip;
            pose.frames[pose.count] = phase * ClipCycle(hiClip);
            pose.weights[pose.count] = weight * between;
            total += pose.weights[pose.count];
            ++pose.count;
        }
    }

    // Nothing weighted at this time -- every ramp still ahead of it, say -- holds slot 0's node,
    // which a record always names one in.
    if (pose.count == 0)
    {
        BlendNode node = tables.nodes.Get(rig.nodes, slots[0].nodeIndex);
        let clipIndex = node.kind == BlendNodeKind.kClip ? node.clip : tables.samples.Get(rig.blendSamples, node.firstSample).clip;
        Clip clip = tables.clips.Get(rig.clips, clipIndex);
        return SingleClipPose(clip, SlotFrames(clip, slots[0], time));
    }

    for (uint s = 0; s < pose.count; ++s)
    {
        pose.weights[s] /= total;
    }
    return pose;
}

// The entry of `pose` carrying the most weight: what an automatic instance draws from its rig's
// table, where no blend can live. The first of equals, so a tie resolves the same way every frame.
//
// @pre pose.count > 0, which ResolveSlots guarantees.
public uint DominantEntry(LocalPose pose)
{
    uint best = 0;
    for (uint s = 1; s < pose.count; ++s)
    {
        if (pose.weights[s] > pose.weights[best])
        {
            best = s;
        }
    }
    return best;
}
