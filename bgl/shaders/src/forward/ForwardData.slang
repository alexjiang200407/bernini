import types.BaseInstance;
import idl.Meshlet;
import idl.Vertex;
import types.ComputeBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import idl.IGeom;
import idl.IMeshInstance;
import types.RangeBuffer;

public struct ForwardData<T>
    where T : IMeshInstance
{
    public float4x4 viewProj;
    public uint psoIndex;
    public PackedBuffer<BaseInstance<T>> instanceBuffer;
    public EntryBuffer<T> meshBuffer;
    public EntryBuffer<T::GeomType> geomBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public RangeBuffer<Vertex> vertexBuffer;
    public RangeBuffer<uint> indexBuffer;
    // These are UAV-only ComputeBuffers (the compaction pass writes them); read them
    // through the UAV here so the bindless handle matches the descriptor type. Reading
    // a RWStructuredBuffer is valid, and there is no SRV descriptor to bind instead.
    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;
};
