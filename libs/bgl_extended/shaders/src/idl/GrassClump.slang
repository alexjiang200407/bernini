// One point a clump of blades grows from, in the mesh's space: the position and its height scale in
// w, the ground normal octahedral-encoded as two snorm16, and a linear RGBA8 tint.
public struct GrassClump
{
    public float4 position;
    public uint normal;
    public uint color;
    public uint pad0;
    public uint pad1;
};
