// One grass field of one visible placement, in a view's grass list: which MeshInstance places it and
// which GrassLook draws it. The list's chunk references name these by index.
public struct GrassDraw
{
    public uint mesh;
    public uint look;
};
