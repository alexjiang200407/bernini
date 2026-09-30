import Range;
import RangeWithCount;
import RawRange;
import Meshlet;
import MeshletGroup;
import VertexLayout;

public struct Submesh
{
    public VertexLayout layout;
    public RangeWithCount<Meshlet> meshlets;
    public Range<uint> vertexMap;
    public RawRange vertexData;
    public Range<uint> indices;
    public uint vertexCount;

    // Local space, like Meshlet's; the world bound is derived per instance from the placement's
    // transform.
    // xyz is the centre, w the radius.
    public float4 boundingSphere;

    // One bound per cMeshletsPerGroup meshlets; the count follows from the meshlet count, so the
    // range carries none of its own. Last because a float4 is 16-byte aligned under MSL and a word
    // ahead of it would move it off that.
    public Range<MeshletGroup> meshletGroups;

    public uint GetMeshletCount() { return meshlets.count; }
};
