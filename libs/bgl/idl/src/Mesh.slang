import RangeWithCount;
import Submesh;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;
};
