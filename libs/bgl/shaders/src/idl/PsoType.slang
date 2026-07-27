// THIS IS A FILE GENERATED FROM PsoType.slang. DO NOT EDIT MANUALLY

// 32-bit, not uint16_t: core WGSL has no 16-bit scalar, and a 16-bit enum reaches the WGSL backend
// as an empty type -- the shaders pass PsoType by value. Nothing stores it at a fixed width.
//
// Spelled uint32_t rather than uint because idlgen copies enum and constant initializers across
// verbatim, so the cast has to be valid C++ as well as Slang.
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
    kTransparentOcclude_StaticMesh_PBR,
    kTransparentOcclude_StaticMesh_LoosePbr,
    kAssert_StaticMesh,
    kCount,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);
