public enum MaterialType : uint32_t
{
    kInvalid = uint32_t(-1), // sentinel for an unset material handle
    kNull = 0,               // unlit white color, no textures, no lighting.
    kAssert,                 // debug: pixel shader raises a GPU assertion (dbg_raise). No-op in Release.
    kPBR,
    kLoosePbr, // unbaked, per-channel PBR material (editor / channel-viz); bakes to kPBR.
    // The reserved game surface slots, one kind each from here: a surface registered into slot N
    // tags its records kGameStart + N, and the slot's rows draw it. A slot nothing filled draws
    // the null surface. cGameSlots of them, so kCount is the literal the C++ side asserts.
    kGameStart,
    kCount = 8,
}
;

public static const uint32_t cGameSlots = 4;
