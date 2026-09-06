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
    // Three rows per reserved game slot, in this order within each: GetPsoFromGeomAndMaterial adds
    // the layer's offset to the slot's opaque row.
    kOpaque_StaticMesh_Game0,
    kAlphaTest_StaticMesh_Game0,
    kTransparent_StaticMesh_Game0,
    kOpaque_StaticMesh_Game1,
    kAlphaTest_StaticMesh_Game1,
    kTransparent_StaticMesh_Game1,
    kOpaque_StaticMesh_Game2,
    kAlphaTest_StaticMesh_Game2,
    kTransparent_StaticMesh_Game2,
    kOpaque_StaticMesh_Game3,
    kAlphaTest_StaticMesh_Game3,
    kTransparent_StaticMesh_Game3,
    kCount,
}
;

public static const uint32_t c_PsoCount = uint32_t(PsoType::kCount);
