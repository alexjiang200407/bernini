import bgl.MeshInstanceWriter;

// Places slot i at positions[i] for the first `shown` slots, still; hides the rest. What a crowd's
// writer is, minus the crowd: positions another owner wrote, imported and bound here.
public struct MeshInstanceWriterFromBuffer : IMeshInstanceWriter
{
    public struct Params
    {
        public StructuredBuffer<float4>.Handle positions;
        public uint shown;
    };

    public static void Write<B : IMeshInstanceBlock>(B block, Params params, uint slot)
    {
        if (slot >= params.shown)
        {
            block.Hide(slot);
            return;
        }

        let p = params.positions[slot].xyz;
        let transform = float4x4(1.0, 0.0, 0.0, p.x, 0.0, 1.0, 0.0, p.y, 0.0, 0.0, 1.0, p.z, 0.0, 0.0, 0.0, 1.0);
        block.Place(slot, transform, transform);
    }
};
