import RawTextureHandle;

// A geom's baked impostor as the impostor stage reads it: assetlib::MeshImpostor, with its two
// atlases uploaded. The tier past the geom's last level of detail, drawn as one quad per placement.
// The payload of a record in the impostor arena (Geom.impostor); its floor is Geom's.
public struct Impostor
{
    // Handles first and contiguous, as in PbrMaterial, so a raw-loaded record samples them through
    // the arena's typed view of the same allocation. Both RGBA8 UNORM, c_ImpostorAtlasMips mips.
    public RawTextureHandle albedo;      // linear base colour, coverage in a
    public RawTextureHandle normalDepth; // octahedral local normal in rg, occlusion in b, depth in a

    // The sphere every frame was baked over, in the geom's local space: xyz centre, w radius.
    public float4 sphere;

    // Frames along each side of the atlas, which is the upper hemisphere's octahedral grid.
    public uint framesPerSide;

    // The whole impostor's, averaged at the bake: no texel needs its own at an impostor's size.
    public float roughness;
    public float metallic;
};
