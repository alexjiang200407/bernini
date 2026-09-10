public enum PsoType : uint32_t
{
    kInvalid = uint32_t(-1),
    kOpaque_StaticMesh_Null = 0,
    kOpaque_StaticMesh_PBR,
    kOpaque_StaticMesh_LoosePbr,
    kAlphaTest_StaticMesh_PBR,
    kAlphaTest_StaticMesh_LoosePbr,
    kTransparent_StaticMesh_PBR,
    kTransparent_StaticMesh_LoosePbr,
    kHashedAlpha_StaticMesh_PBR,
    kHashedAlpha_StaticMesh_LoosePbr,
    kAssert_StaticMesh,
    kOpaque_SkinnedMesh_PBR,
    kAlphaTest_SkinnedMesh_PBR,
    kHashedAlpha_SkinnedMesh_PBR,
    kTransparent_SkinnedMesh_PBR,
    // The reserved game slots' rows, cGameSlots of them from here, seven each: a tier's opaque,
    // alpha-test and hashed rows in that order, the static tier then the skinned, then the one
    // transparent row both tiers share. A slot's row is kGameRowsStart + cGameSlotRows * slot + the
    // offset GameSlotRow derives, and kCount is the literal the C++ side asserts.
    kGameRowsStart,
    kCount = 42,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);

// A slot's rows, in PsoType order.
public static const uint32_t cGameSlotRows = 7;

// The geometry tiers a game surface draws on: static and skinned.
public static const uint32_t cGameSlotTiers = 2;

// The layers a tier has a row of its own for, which is every layer but blended.
public static const uint32_t cGameSlotTierRows = 3;

// Where a slot's transparent row sits in its block. One row rather than one per tier: the blended
// pipeline's geometry stage is AnyMesh, which branches tier per instance, so a second row would
// name the same pipeline -- which is what kTransparent_SkinnedMesh_PBR already does for kPBR.
public static const uint32_t cGameSlotBlendRow = 6;

// The row arithmetic, written once for the shaders. The C++ side mirrors these in bgl's util.h,
// and PsoSelection_test walks every slot and layer to hold the two together -- a shader deriving a
// row by hand is how the CPU and the GPU come to disagree about which bucket an instance is in.

/// The first of `slot`'s rows.
public uint32_t GameSlotRowBase(uint32_t slot)
{
    return uint32_t(PsoType::kGameRowsStart) + slot * cGameSlotRows;
}

/// Whether `pso` is one of the reserved game slots' rows at all.
public bool IsGameRow(uint32_t pso)
{
    return pso >= uint32_t(PsoType::kGameRowsStart) && pso < c_PsoCount;
}

/// Which of its slot's rows `pso` is. @pre IsGameRow(pso).
public uint32_t GameRowOffset(uint32_t pso)
{
    return (pso - uint32_t(PsoType::kGameRowsStart)) % cGameSlotRows;
}
