import types.SubmeshInstance;
import idl.Mesh;
import idl.Meshlet;
import idl.Submesh;
import types.ByteBuffer;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import types.RangeBuffer;

public struct ForwardData
{
    public float4x4 viewProj;
    public uint psoIndex;

    // Where this draw's slice of the instance-index list starts. The opaque path adds the GPU
    // prefix-sum base on top (usePrefixSumBase != 0); the transparent path passes a CPU-computed
    // run offset directly (usePrefixSumBase == 0).
    public uint instanceListBase;
    public uint usePrefixSumBase;

    public PackedBuffer<SubmeshInstance> instanceBuffer;
    public EntryBuffer<Mesh> meshBuffer;
    public RangeBuffer<Submesh> submeshBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public ByteBuffer vertexDataBuffer;
    public RangeBuffer<uint> indexBuffer;
    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;
};
