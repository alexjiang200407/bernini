public enum ErrorCode : uint
{
    kUnknown = 1,
    kInvalidVertexLayout,
    kInvalidSubmeshIndex,
    kInvalidMeshletIndex,
    kMeshletVertexOverflow,
    kMeshletPrimitiveOverflow,
    kInvalidVertexIndex,
    kInvalidSubmeshInstance,
    kInvalidPsoType,
    // Offset 0 is the reserved null element, so these read zeroes rather than faulting. Defined,
    // but never what the caller meant -- the check is what keeps that from shading silently.
    kNullEntryDeref,
    kNullRangeDeref,
}
;
