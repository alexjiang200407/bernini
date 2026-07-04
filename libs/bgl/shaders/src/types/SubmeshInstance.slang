import types.PsoType;
import idl.Entry;
import idl.Mesh;

public struct SubmeshInstance
{
    public PsoType psoType;
    public Entry<Mesh> meshInstance;
    public uint submeshIndex;
};
