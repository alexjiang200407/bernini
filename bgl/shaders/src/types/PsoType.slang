

public enum PsoType : uint16_t
{
    kInvalid = uint16_t(-1),
    kOpaque_StaticMesh_Null = 0,
    kOpaque_StaticMesh_PBR,
    kAlphaTest_StaticMesh_PBR,
    kTransparent_StaticMesh_PBR,
    kCount,
};
