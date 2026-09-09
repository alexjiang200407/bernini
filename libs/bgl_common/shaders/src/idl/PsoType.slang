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
    // The reserved game slots' rows, cGameSlots of them from here, five each in this order: the
    // static tier's opaque and alpha-test, the skinned tier's opaque and alpha-test, then the one
    // transparent row both tiers share. A slot's row is kGameRowsStart + cGameSlotRows * slot + the
    // offset GameSlotRow derives, and kCount is the literal the C++ side asserts.
    kGameRowsStart,
    kCount = 34,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);

// A slot's rows, in PsoType order.
public static const uint32_t cGameSlotRows = 5;

// Where a slot's transparent row sits in its block. One row rather than one per tier: the blended
// pipeline's geometry stage is AnyMesh, which branches tier per instance, so a second row would
// name the same pipeline -- which is what kTransparent_SkinnedMesh_PBR already does for kPBR.
public static const uint32_t cGameSlotBlendRow = 4;
