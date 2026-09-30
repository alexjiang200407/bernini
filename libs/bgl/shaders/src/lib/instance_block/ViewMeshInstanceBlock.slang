import bgl.InstanceWriter;
import idl.MeshInstance;
import idl.MeshInstanceFlag;

// An instance block over the view's MeshInstance buffer: slots [first, first + capacity).
// ScalarDataLayout for the same reason as lib.types.EntryBuffer: the C++ mirror packs MeshInstance
// tightly, and the default structured layout would pad its nested handles.
public struct ViewMeshInstanceBlock : IMeshInstanceBlock
{
    public RWStructuredBuffer<MeshInstance, ScalarDataLayout>.Handle meshes;
    public uint first;
    public uint capacity;

    public uint Capacity() { return capacity; }

    public void Place(uint slot, float4x4 transform, float4x4 prevTransform)
    {
        let index = first + slot;
        [ForceUnroll]
        for (int row = 0; row < 3; ++row)
        {
            meshes[index].transform[row] = transform[row];
            meshes[index].prevTransform[row] = prevTransform[row];
        }
        meshes[index].flags &= ~uint(MeshInstanceFlag.kHidden);
    }

    public void Hide(uint slot) { meshes[first + slot].flags |= uint(MeshInstanceFlag.kHidden); }
};
