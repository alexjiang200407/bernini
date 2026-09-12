import RawTextureHandle;

// How many texture handles every game surface's record carries -- a fixed count, not a ceiling a
// surface comes in under: the eight are always there so the parameters after them start at a
// compile-time offset whatever the surface declared. Raising it costs 56 bytes a slot on every
// surface material (8 for the whole handle, 32 for the four route handles, 16 for their channel
// selectors) and moves the offsets below with it.
public static const uint cGameSurfaceTextureSlots = 8;

// How many channel routes a data slot gathers from: one per component of the float4 a sample
// returns. Not a knob -- the count is the width of the sample.
public static const uint cGameSurfaceRouteChannels = 4;

// Where a slot's channel selectors start, measured as cGameSurfaceParamsByteOffset is: past
// cRawPayloadOffset, at the end of the two handle arrays -- which sit first and contiguous, as in
// LoosePbrMaterial, so the arena's typed view reaches every route by index. The C++ side asserts
// the arithmetic.
public static const uint cGameSurfaceRouteChannelsByteOffset = 336;

// Where `routedMask` sits, measured as the two offsets around it are. The C++ side asserts the
// arithmetic.
public static const uint cGameSurfaceRoutedMaskByteOffset = 476;

// Where a game surface's parameters start, measured from the record's header: past
// cRawPayloadOffset and this struct. The C++ side asserts the arithmetic.
public static const uint cGameSurfaceParamsByteOffset = 480;

// What `coverageSlot` holds for a surface declaring no texture hashed alpha could measure against.
// Only the hashed rows read the field, and a material reaches one only through a surface that has a
// carrier, so this never indexes a sample -- it is what makes a record that cannot be drawn hashed
// say so.
public static const uint cNoCoverageSlot = 0xFFFFFFFFu;

// The engine's fixed part of a game surface's payload; the surface's own parameters follow it.
public struct GameSurfaceRecord
{
    public RawTextureHandle textures[cGameSurfaceTextureSlots];

    // A data slot's four channel routes (ADR-11 of the surface-material-panel plan): the reader
    // gathers component c of slot i from routeTextures[i * cGameSurfaceRouteChannels + c]'s
    // routeChannels-named component, exactly as LoosePbrMaterial routes the engine's own
    // channels. A whole binding is the identity routing -- the bound texture four times,
    // channels 0..3 -- so there is one record shape and one sampling path. Only the reader's
    // DataSlot specialization reads these; the other slot kinds sample `textures` whole.
    public RawTextureHandle routeTextures[cGameSurfaceTextureSlots * cGameSurfaceRouteChannels];

    public uint routeChannels[cGameSurfaceTextureSlots * cGameSurfaceRouteChannels];

    // Non-zero draws a non-opaque surface's back faces; the mesh stage culls them otherwise.
    public uint doubleSided;

    // Only read by the alpha-test PSOs, against the surface's Coverage.
    public float alphaCutoff;

    // Which of `textures` hashed alpha measures its minification against, or cNoCoverageSlot. The
    // surface declares it by slot kind and the engine resolves it; see bgl::CoverageCarrierSlot.
    public uint coverageSlot;

    // Bit i set means slot i is composed from its routes; clear, the slot samples `textures[i]`
    // whole -- the fast path every whole binding (the shipped, baked case among them) keeps.
    // Written at record build, so the reader's branch is one scalar load, uniform per material.
    public uint routedMask;
};
