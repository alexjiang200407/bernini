import idl.Entry;
import idl.Mesh;
import idl.IMaterial;

// The CPU mirror is bgl::SubmeshInstance (src/types/SubmeshInstance.h) -- keep the two in step.
public struct SubmeshInstance
{
    public Entry<Mesh> meshInstance;
    public uint submeshIndex;
    public Entry<IMaterial> material;
    public uint pso;
};
