import RawTextureHandle;

// A geom's baked impostor as the impostor stage reads it: assetlib::MeshImpostor, with its two
// atlases uploaded. The tier past the geom's last level of detail, drawn as one quad per placement.
public struct Impostor
{
    // Handles first and contiguous, as in PbrMaterial, so a raw-loaded record samples them through
    // a typed view of the same allocation. Both RGBA8 UNORM, c_ImpostorAtlasMips mips.
    public RawTextureHandle albedo;      // linear base colour, coverage in a
    public RawTextureHandle normalDepth; // local normal * 0.5 + 0.5, depth toward the viewer in a

    // The sphere every frame was baked over, in the geom's local space: xyz centre, w radius.
    public float4 sphere;

    // Drawn while the placement's projected diameter, in pixels, is at least this; 0 never drops it.
    public float minPixels;

    // Frames along each side of the atlas, which is the upper hemisphere's octahedral grid.
    public uint framesPerSide;
};
