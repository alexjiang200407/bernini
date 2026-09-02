import idl.Entry;
import idl.IMaterial;
import idl.MeshInstance;
import idl.RawEntry;

// The CPU mirror is bgl::SubmeshInstance (src/types/SubmeshInstance.h) -- keep the two in step.
public struct SubmeshInstance
{
    public Entry<MeshInstance> meshInstance;
    public uint submeshIndex;
    public RawEntry<IMaterial> material;
    public uint pso;
};
