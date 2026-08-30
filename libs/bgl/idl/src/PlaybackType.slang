public enum PlaybackType : uint8_t
{
    kInvalid = uint8_t(-1), // sentinel for an unset playback handle
    kNone = 0,              // what the arena's reserved null record reads as; a static placement has no record
    kVat,                   // a pose fetched from a baked texture pair
    kSkinned,               // a pose computed on the GPU into a bone palette
    kCount,
}
;
