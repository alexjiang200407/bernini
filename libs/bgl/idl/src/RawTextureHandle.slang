// A texture handle in the byte domain: the same eight bytes idl::TextureHandle occupies, with no
// resource type declared.
//
// A struct that declares one cannot be read out of a raw buffer at all on Metal -- the element type
// of a bindless buffer is fixed at its declaration, and a raw one declares bytes, so Slang rebuilds
// the struct from loaded scalars and MSL will not make a texture of an integer. A record therefore
// stores this, and a second, typed view of the same allocation is what samples it.
public struct RawTextureHandle
{
    public uint2 bits;
};
