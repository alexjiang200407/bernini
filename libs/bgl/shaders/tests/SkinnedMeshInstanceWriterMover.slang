import bgl.MeshInstanceWriter;

// The mover's skinned twin: the first `shown` slots in a row from `origin`, `spacing` apart along x,
// each drawn last frame `motion` behind where it is now and playing `offsetStep` seconds ahead of
// the slot before it; the rest hidden.
public struct SkinnedMeshInstanceWriterMover : ISkinnedMeshInstanceWriter
{
    public struct Params
    {
        public float3 origin;
        public float spacing;
        public float3 motion;
        public uint shown;
        public float offsetStep;
    };

    static float4x4 Translate(float3 p) { return float4x4(1.0, 0.0, 0.0, p.x, 0.0, 1.0, 0.0, p.y, 0.0, 0.0, 1.0, p.z, 0.0, 0.0, 0.0, 1.0); }

    public static void Write<B : ISkinnedMeshInstanceBlock>(B block, Params params, uint slot)
    {
        if (slot >= params.shown)
        {
            block.Hide(slot);
            return;
        }

        let p = params.origin + float3(params.spacing * float(slot), 0.0, 0.0);
        block.Place(slot, Translate(p), Translate(p - params.motion));
        block.SetPlaybackOffset(slot, float(slot) * params.offsetStep);
    }
};
