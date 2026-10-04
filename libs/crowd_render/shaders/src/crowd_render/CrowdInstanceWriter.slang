import bgl.MeshInstanceWriter;
import crowd.idl.RenderAgent;
import crowd_render.idl.WriterFrame;

/**
 * What a crowd writer reads for one block, the same for a static agent type's and a skinned one's.
 */
public struct CrowdWriterParams
{
    public StructuredBuffer<RenderAgent>.Handle ring;
    public TickRecords current;
    public TickRecords previous;
    public TickRecords older;
    public float alpha;
    public float lastAlpha;
    public uint lastFrame;

    // The type's run in the current tick, its firstRecordIndex counted from the tick's first record.
    public TickRecords run;

    // The type's mesh in the agent's frame: +z its facing, y up, the origin on the ground.
    public float4x4 model;

    // Seconds a skinned type's agents are spread over by id; read by the skinned writer alone.
    public float phaseSpreadSeconds;
};

/**
 * Places one agent type's agents of a crowd's render ring, interpolated: slot i is the i-th record
 * of the type's run in the current tick, drawn `alpha` of the way from the previous tick, and
 * hidden past the run. Its motion is last frame's pose of the same agent, found by
 * following `source` back, so it holds across a split, a merge or a destroy that moved the agent.
 */
public struct CrowdPlacement
{
    struct Pose
    {
        float2 position;
        float2 facing;
    };

    // Agent `index` of `tick`, `t` of the way from where it was in `before`; where it is for an
    // agent `before` did not hold.
    static Pose At(CrowdWriterParams params, TickRecords tick, TickRecords before, uint index, float t)
    {
        let now = params.ring[tick.firstRecordIndex + index];
        var pose : Pose;
        pose.position = now.position;
        pose.facing = now.facing;
        if (now.source == c_RenderSpawned || now.source >= before.count)
            return pose;

        let then = params.ring[before.firstRecordIndex + now.source];
        pose.position = lerp(then.position, now.position, t);
        let facing = lerp(then.facing, now.facing, t);
        let size = length(facing);
        if (size > 1e-6)
            pose.facing = facing / size;
        return pose;
    }

    static float4x4 Transform(CrowdWriterParams params, Pose pose)
    {
        let f = pose.facing;
        let p = pose.position;
        let agent = float4x4(f.y, 0.0, f.x, p.x, 0.0, 1.0, 0.0, 0.0, -f.x, 0.0, f.y, p.y, 0.0, 0.0, 0.0, 1.0);
        return mul(agent, params.model);
    }

    /** Places or hides `slot`, and whether it placed it. */
    public static bool Place<B : IMeshInstanceBlock>(B block, CrowdWriterParams params, uint slot)
    {
        if (slot >= params.run.count)
        {
            block.Hide(slot);
            return false;
        }

        let index = params.run.firstRecordIndex + slot;
        let pose = At(params, params.current, params.previous, index, params.alpha);
        var last = pose;
        if (params.lastFrame == c_LastFrameSameTick)
        {
            last = At(params, params.current, params.previous, index, params.lastAlpha);
        }
        else if (params.lastFrame == c_LastFrameTickBefore)
        {
            let source = params.ring[params.current.firstRecordIndex + index].source;
            if (source != c_RenderSpawned && source < params.previous.count)
                last = At(params, params.previous, params.older, source, params.lastAlpha);
        }

        block.Place(slot, Transform(params, pose), Transform(params, last));
        return true;
    }

    /**
     * The share of the spread an agent plays ahead, fixed by its id for its life: the id times the
     * golden ratio's fraction of 2^32, wrapped, so consecutive ids land far apart and every bit of
     * a large id still counts.
     */
    public static float PhaseShare(uint id) { return float(id * 0x9E3779B9u) * (1.0 / 4294967296.0); }
};

/** A static agent type's writer: the agents placed, and nothing else. */
public struct CrowdInstanceWriter : IMeshInstanceWriter
{
    public typealias Params = CrowdWriterParams;

    public static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot) { CrowdPlacement.Place(block, params, slot); }
};

/**
 * A skinned agent type's writer: the agents placed as the static writer places them, each playing
 * the block's record its id's share of the spread ahead of the clock.
 */
public struct SkinnedCrowdInstanceWriter : ISkinnedMeshInstanceWriter
{
    public typealias Params = CrowdWriterParams;

    public static void Write<B : ISkinnedMeshInstanceBlock>(B block, Params params, uint slot)
    {
        if (!CrowdPlacement.Place(block, params, slot))
            return;
        let id = params.ring[params.current.firstRecordIndex + params.run.firstRecordIndex + slot].id;
        block.SetPlaybackOffset(slot, CrowdPlacement.PhaseShare(id) * params.phaseSpreadSeconds);
    }
};
