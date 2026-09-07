/**
 * A texture the material binds by name, of the kind the bake will treat it as. The index is written
 * by the engine when it packs the record; a surface declares the field and never sets it.
 */
public interface ISlot
{
    uint Index();
};

/// Colour data: sRGB-encoded, alpha kept where the layer reads it.
public struct ColorSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
};

/// Linear data: occlusion, roughness, metallic, masks.
public struct DataSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
};

/// A tangent-space normal map, its xy as PbrSurface.normalXY reads them.
public struct NormalSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
};

/// Coverage alone, for a surface whose alpha lives apart from its colour.
public struct CoverageSlot : ISlot
{
    public uint index;
    public uint Index() { return index; }
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
 * What the engine hands a surface: every dereference already done on the other side. A slot is
 * sampled by the index the engine wrote into it; the interpolants and the camera come from the
 * fragment being shaded.
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
