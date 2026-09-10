import RawTextureHandle;

// How many texture handles every game surface's record carries -- a fixed count, not a ceiling a
// surface comes in under: the eight are always there so the parameters after them start at a
// compile-time offset whatever the surface declared. Raising it costs 8 bytes a slot on every
// surface material and moves cGameSurfaceParamsByteOffset with it.
public static const uint cGameSurfaceTextureSlots = 8;

// Where a game surface's parameters start, measured from the record's header: past
// cRawPayloadOffset and this struct. The C++ side asserts the arithmetic.
public static const uint cGameSurfaceParamsByteOffset = 96;

// The engine's fixed part of a game surface's payload; the surface's own parameters follow it.
public struct GameSurfaceRecord
{
    public RawTextureHandle textures[cGameSurfaceTextureSlots];

    // Non-zero draws a non-opaque surface's back faces; the mesh stage culls them otherwise.
    public uint doubleSided;

    // Only read by the alpha-test PSOs, against the surface's Coverage.
    public float alphaCutoff;

    public uint pad[2];
};
