public enum MaterialType : uint8_t
{
    kInvalid = uint8_t(-1), // sentinel for an unset material handle
    kNull = 0,              // unlit white color, no textures, no lighting.
    kAssert,                // debug: pixel shader raises a GPU assertion (dbg_raise). No-op in Release.
    kPBR,
    kLoosePbr, // unbaked, per-channel PBR material (editor / channel-viz); bakes to kPBR.
    kCount,
}
;
