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
    kCount,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);
