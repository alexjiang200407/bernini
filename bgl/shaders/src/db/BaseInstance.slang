import db.PsoType;
import db.Entry;
import intfc.IMeshInstance;
import intfc.IMaterial;

public struct BaseInstance<G = IMeshInstance, M = IMaterial>
    where G : IMeshInstance
    where M : IMaterial
{
    public PsoType psoType;
    public Entry<G> meshInstance;
    public Entry<M> materialInstance;
};
