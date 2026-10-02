import bgl.MeshInstanceWriter;
import crowd.idl.RenderAgent;
import crowd_render.idl.WriterFrame;

/**
 * Places one agent type's agents of a crowd's render ring, interpolated: slot i is the i-th record
 * of the type's run in the current tick, drawn `alpha` of the way from the previous tick, and
 * hidden past the run. Its motion is last frame's pose of the same agent, found by
 * following `source` back, so it holds across a split, a merge or a destroy that moved the agent.
 */
public struct CrowdInstanceWriter : IMeshInstanceWriter
{
    public struct Params
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
    };

    struct Pose
    {
        float2 position;
        float2 facing;
    };

    // Agent `index` of `tick`, `t` of the way from where it was in `before`; where it is for an
    // agent `before` did not hold.
    static Pose At(Params params, TickRecords tick, TickRecords before, uint index, float t)
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

    static float4x4 Transform(Params params, Pose pose)
    {
        let f = pose.facing;
        let p = pose.position;
        let agent = float4x4(f.y, 0.0, f.x, p.x, 0.0, 1.0, 0.0, 0.0, -f.x, 0.0, f.y, p.y, 0.0, 0.0, 0.0, 1.0);
        return mul(agent, params.model);
    }

    public static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot)
    {
        if (slot >= params.run.count)
        {
            block.Hide(slot);
            return;
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
    }
};
