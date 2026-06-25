import db.Range;
import db.Meshlet;

public interface IGeom
{
    public uint GetMeshletCount();
    public RangeWithCount<Meshlet> GetMeshletRange();
};
