/**
 * A texture the material binds by name, of the kind the bake will treat it as. The index is written
 * by the engine when it packs the record; a surface declares the field and never sets it.
 *
 * Routed says how a sample resolves: a routed kind's material may compose the slot from single
 * channels of other textures, and the reader gathers them; an unrouted kind samples one texture
 * whole. Compile-time per kind, so each Sample specializes to one path.
 */
public interface ISlot
{
    uint Index();
    static bool Routed();
};

/// Colour data: sRGB-encoded, alpha kept where the layer reads it.
public struct ColorSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
    public static bool Routed() { return false; }
};

/// Linear data: occlusion, roughness, metallic, masks. The one routed kind: a material may bind
/// it whole or compose it from channel routes, and either way the record carries four routes --
/// a whole binding is the identity routing -- which the reader gathers.
public struct DataSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
    public static bool Routed() { return true; }
};

/// A tangent-space normal map, its xy as PbrSurface.normalXY reads them.
public struct NormalSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
    public static bool Routed() { return false; }
};

/// Coverage alone, for a surface whose alpha lives apart from its colour.
public struct CoverageSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
    public static bool Routed() { return false; }
};

/**
 * The value a parameter takes when a material does not set it, read off the field at
 * registration. Up to four components; a scalar uses the first.
 */
[__AttributeUsage(_AttributeTargets.Var)]
public struct DefaultAttribute
{
    float x;
    float y = 0.0;
    float z = 0.0;
    float w = 0.0;
};

/**
 * Marks a float3 or float4 value as a colour, so an editor shows a swatch and a picker instead of
 * bare numbers. Presentation only: the record and the shader read the value the same either way.
 * Registration refuses it on a value with fewer than three components.
 */
[__AttributeUsage(_AttributeTargets.Var)]
public struct ColorAttribute
{};

/**
 * What the engine hands a surface: every dereference already done on the other side. A slot is
 * sampled by the index the engine wrote into it; the interpolants and the camera come from the
 * fragment being shaded.
 *
 * WorldNormal is the normal of the face being shaded: on a double-sided material a back face
 * reads the interpolated normal negated, the same flip the engine applies to its own lighting.
 * A view-dependent term written against it is therefore correct on both faces without the
 * surface ever seeing a facing bit.
 */
public interface IMaterialReader
{
    float4 Sample<S : ISlot>(S slot, float2 uv);
    float2 Uv();
    float3 WorldPos();
    float3 WorldNormal();
    float4 WorldTangent();
    float3 CameraPos();
};
