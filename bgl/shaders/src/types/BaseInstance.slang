import types.PsoType;
import idl.Entry;
import idl.IMeshInstance;
import idl.IMaterial;

public struct BaseInstance<G = IMeshInstance, M = IMaterial>
    where G : IMeshInstance
    where M : IMaterial
{
    public PsoType psoType;
    public Entry<G> meshInstance;
    public Entry<M> materialInstance;
};
