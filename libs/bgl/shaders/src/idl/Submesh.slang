// THIS IS A FILE GENERATED FROM Submesh.slang. DO NOT EDIT MANUALLY

import Range;
import RangeWithCount;
import Meshlet;
import Entry;
import IMaterial;
import VertexLayout;

public struct Submesh
{
    public VertexLayout layout;
    public RangeWithCount<Meshlet> meshlets;
    public Range<uint> vertexMap;
    public Range<uint> vertexData;
    public Range<uint> indices;
    public uint vertexCount;
    public Entry<IMaterial> material;

    public RangeWithCount<Meshlet> GetMeshletRange() { return meshlets; }

    public uint GetMeshletCount() { return meshlets.count; }
};
