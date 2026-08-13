import Entry;
import RangeWithCount;
import Submesh;
import VatState;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;

    // Null for a static mesh; the VAT mesh shader is the only reader.
    public Entry<VatState> vatState;
};
