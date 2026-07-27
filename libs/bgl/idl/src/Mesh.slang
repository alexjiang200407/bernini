import RangeWithCount;
import Submesh;

public struct Mesh
{
    public float4x4 transform;
    public RangeWithCount<Submesh> submeshes;

    // WGSL rounds a struct's size up to its alignment, which the float4x4 makes 16 -- so the GPU
    // stride is 80 however tightly the CPU mirror packs. Padded explicitly so both sides agree on
    // it; without this the second element of the mesh buffer reads from the wrong offset.
    public uint2 pad;
};
