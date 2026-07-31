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
    // xyz is the centre, w the radius. One float4 rather than a float3 and a float because MSL
    // sizes a float3 at 16 bytes where the C/C++ mirror gives it 12, which puts every following
    // member at a different offset on Metal -- a mismatch no trailing padding can close.
    public float4 boundingSphere;

    public uint GetMeshletCount() { return meshlets.count; }
};
