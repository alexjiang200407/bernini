// THIS IS A FILE GENERATED FROM Submesh.slang. DO NOT EDIT MANUALLY

import Range;
import RangeWithCount;
import RawRange;
import Meshlet;
import VertexLayout;

public struct Submesh
{
    public VertexLayout layout;
    public RangeWithCount<Meshlet> meshlets;
    public Range<uint> vertexMap;
    public RawRange vertexData;
    public Range<uint> indices;
    public uint vertexCount;

    // Local space, like Meshlet's; the world bound is derived per instance from Mesh.transform.
    // xyz is the centre, w the radius.
    public float4 boundingSphere;

    public uint GetMeshletCount() { return meshlets.count; }
};
