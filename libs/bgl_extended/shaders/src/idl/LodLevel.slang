// A mesh's level of detail, level 0 its full detail. Every place a level is named takes one of
// these rather than a bare index, so a submesh index or a count cannot be passed where a level is
// meant; a level chosen at runtime is converted once, where it is chosen.
public enum LodLevel : uint32_t
{
    kLod0,
    kLod1,
    kLod2,
    kLod3,
    kLod4,
    kLod5,
    kLod6,
    kLod7,
    kCount,
}
;

// Levels one mesh may carry, level 0 included: the length of every per-level table.
public static const uint cMaxMeshLods = uint32_t(LodLevel::kCount);

// Coming back to a finer level needs the placement's size to clear that level's floor by this
// share, so a placement resting on a threshold does not switch every frame.
public static const float cLodHysteresis = 0.15;
