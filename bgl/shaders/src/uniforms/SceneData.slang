import db.BaseInstance;
import db.Meshlet;
import db.Vertex;
import types.EntryBuffer;
import types.PackedBuffer;
import intfc.IGeom;
import intfc.IMeshInstance;
import types.RangeBuffer;

public struct SceneData<T>
    where T : IMeshInstance
{
    public float4x4 viewProj;
    public PackedBuffer<BaseInstance<T>> instanceBuffer;
    public EntryBuffer<T> meshBuffer;
    public EntryBuffer<T::GeomType> geomBuffer;
    public RangeBuffer<Meshlet> meshletBuffer;
    public RangeBuffer<uint> vertexMapBuffer;
    public RangeBuffer<Vertex> vertexBuffer;
    public RangeBuffer<uint> indexBuffer;
};
