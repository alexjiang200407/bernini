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
    // The reserved game slots' rows, cGameSlots of them from here, three each in this order: opaque,
    // alpha-test, transparent. A slot's row is kGameRowsStart + 3 * slot + the layer's offset, and
    // kCount is the literal the C++ side asserts.
    kGameRowsStart,
    kCount = 26,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);

// A slot's three rows, in PsoType order.
public static const uint32_t cGameSlotRows = 3;
