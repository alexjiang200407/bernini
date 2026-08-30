// One resource handle, as a bindless buffer's element type.
//
// A buffer of bare handles cannot be declared on Metal: `StructuredBuffer<Texture2D.Handle>.Handle`
// lowers to `device texture2d*`, and MSL refuses a pointer to a resource anywhere inside what a
// constant buffer points at -- which is where every such view is bound. The same buffer of a struct
// holding one handle is accepted, and lays out identically.
//
// A constant buffer's own member is the handle itself; this is for the element type alone.
public struct HandleElement<T>
{
    public T handle;
}
