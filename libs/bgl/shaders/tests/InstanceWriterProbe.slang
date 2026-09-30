import bgl.InstanceWriter;

// A writer as a client would write one: the first `shown` slots in a row along x from `origin`,
// standing still, the rest hidden.
public struct InstanceWriterProbe : IInstanceWriter
{
    public struct Params
    {
        public float3 origin;
        public uint shown;
    };

    public static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot)
    {
        if (slot >= params.shown)
        {
            block.Hide(slot);
            return;
        }

        let p = params.origin + float3(float(slot), 0.0, 0.0);
        let transform = float4x4(1.0, 0.0, 0.0, p.x, 0.0, 1.0, 0.0, p.y, 0.0, 0.0, 1.0, p.z, 0.0, 0.0, 0.0, 1.0);
        block.Place(slot, transform, transform);
    }
};
