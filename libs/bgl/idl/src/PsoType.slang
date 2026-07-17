public enum PsoType : uint16_t
{
    kInvalid = uint16_t(-1),
    kOpaque_StaticMesh_Null = 0,
    kOpaque_StaticMesh_PBR,
    kOpaque_StaticMesh_LoosePbr,
    kAlphaTest_StaticMesh_PBR,
    kAlphaTest_StaticMesh_LoosePbr,
    kAssert_StaticMesh,
    kCount,
}
;

public static const uint16_t c_PsoCount = uint16_t(PsoType::kCount);
