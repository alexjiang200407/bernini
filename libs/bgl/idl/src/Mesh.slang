import Entry;
import RangeWithCount;
import SkinnedState;
import Submesh;
import VatState;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;

    // Null for a static mesh; the VAT mesh shader is the only reader.
    public Entry<VatState> vatState;

    // Null for anything but a skinned mesh, and never set together with vatState -- a placement
    // draws through one pipeline family. The skinned mesh shader and the pose pass are the readers.
    public Entry<SkinnedState> skinnedState;
};
