import db.Range;
import db.Vertex;
import db.Meshlet;
import db.Entry;
import intfc.IGeom;
import intfc.IMeshInstance;

public struct StaticGeom : IGeom {
    public Range<uint>             vertexMap;
    public Range<Vertex>           vertices;
    public Range<uint>             indices;
    public RangeWithCount<Meshlet> meshlets;

    public RangeWithCount<Meshlet> GetMeshletRange() {
        return meshlets;
    }

    public uint GetMeshletCount() {
        return meshlets.count;
    }
};

public struct StaticMeshInstance : IMeshInstance {
    public typedef StaticGeom GeomType;

    public Entry<StaticGeom> base;
    public float4x4          transform;

    public uint GetBaseMeshID() {
        return base.GetOffset();
    }
};
