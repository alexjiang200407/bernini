import RangeWithCount;
import Submesh;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;
    public uint totalMeshletCount;

    public uint GetMeshletCount() { return totalMeshletCount; }

    public uint GetSubmeshCount() { return submeshes.count; }
};
