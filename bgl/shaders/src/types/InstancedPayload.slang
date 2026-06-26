import types.BaseInstance;
import idl.IMeshInstance;
import idl.IMaterial;

public struct InstancedPayload<T>
    where T : IMeshInstance
{
    public BaseInstance<T> instance;
}
