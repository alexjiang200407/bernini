import types.SubmeshInstance;
import idl.Mesh;
import idl.Meshlet;
import idl.MeshletInstance;
import idl.Submesh;
import types.ByteBuffer;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import types.RangeBuffer;

public struct ForwardData
{
    public float4x4 viewProj;

    // Equal to viewProj on the first frame.
    public float4x4 prevViewProj;

    public PackedBuffer<SubmeshInstance> instanceBuffer;
    public EntryBuffer<Mesh> meshBuffer;
    public RangeBuffer<Submesh> submeshBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public ByteBuffer vertexDataBuffer;
    public RangeBuffer<uint> indexBuffer;

    // What the expansion pass resolves the amplification stage's work into. The mesh path gets the
    // same thing as a payload and leaves this unread.
    public PackedBuffer<MeshletInstance> meshletInstances;

    // Only the amplification stage reads these, and all three are read-write -- which a WebGPU
    // vertex stage may not bind, so the draw resolves its instance through a record instead.
#if !defined(BGL_WGSL)
    public uint psoIndex;

    // Which GPU-side table holds this draw's base into the instance-index list: the opaque path is
    // bucketed by PSO, the transparent path by occlude partition. Neither base is a CPU value.
    public uint baseSource;
    public uint partitionIndex;

    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;
    public ComputeBuffer<uint> transparentPartitionBase;
#endif
};
