import types.SubmeshInstance;
import idl.Mesh;
import idl.Meshlet;
import idl.Submesh;
import types.RawBuffer;
import types.EntryBuffer;
import types.PackedBuffer;
import types.RangeBuffer;

public struct ForwardData
{
    public PackedBuffer<SubmeshInstance> instanceBuffer;
    public EntryBuffer<Mesh> meshBuffer;
    public RangeBuffer<Submesh> submeshBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public RawBuffer vertexDataBuffer;
    public RangeBuffer<uint> indexBuffer;
};
