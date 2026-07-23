import Range;
import RangeWithCount;
import Meshlet;
import VertexLayout;

public struct Submesh
{
    public VertexLayout layout;
    public RangeWithCount<Meshlet> meshlets;
    public Range<uint> vertexMap;
    public Range<uint> vertexData;
    public Range<uint> indices;
    public uint vertexCount;

    // Local space, like Meshlet's; the world bound is derived per instance from Mesh.transform.
    public float3 boundingCenter;
    public float boundingRadius;

    public uint GetMeshletCount() { return meshlets.count; }
};
