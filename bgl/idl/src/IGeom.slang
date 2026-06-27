import Range;
import Meshlet;

public interface IGeom
{
    public uint GetMeshletCount();
    public RangeWithCount<Meshlet> GetMeshletRange();
};
