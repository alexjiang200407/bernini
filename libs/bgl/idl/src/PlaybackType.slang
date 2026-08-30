public enum PlaybackType : uint32_t
{
    kInvalid = uint32_t(-1), // sentinel for an unset playback handle
    kNone = 0,               // what the arena's reserved null record reads as; a static placement has no record
    kVat,                    // a pose fetched from a baked texture pair
    kSkinned,                // a pose computed on the GPU into a palette of this instance's own
    kSkinnedTable,           // a pose read out of the rig's bone anim table, shared by every instance on it
    kCount,
}
;
