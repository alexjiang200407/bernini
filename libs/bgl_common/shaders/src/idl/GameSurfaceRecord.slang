import RawTextureHandle;

// The texture slots a game surface may declare. Eight handles first and contiguous, so a surface's
// parameters start at a compile-time offset however many slots it declares.
public static const uint cGameSurfaceSlots = 8;

// Where a game surface's parameters start, measured from the record's header: past
// cRawPayloadOffset and this struct. The C++ side asserts the arithmetic.
public static const uint cGameSurfaceParamsOffset = 96;

// The engine's fixed part of a game surface's payload; the surface's own parameters follow it.
public struct GameSurfaceRecord
{
    public RawTextureHandle textures[8];

    // Non-zero draws a non-opaque surface's back faces; the mesh stage culls them otherwise.
    public uint doubleSided;

    // Only read by the alpha-test PSOs, against the surface's Coverage.
    public float alphaCutoff;

    public uint pad0;
    public uint pad1;
};
