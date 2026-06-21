import db.BaseInstance;
import intfc.IMeshInstance;
import intfc.IMaterial;

public struct InstancedPayload<T> where T : IMeshInstance
{
    public BaseInstance<T> instance;
}