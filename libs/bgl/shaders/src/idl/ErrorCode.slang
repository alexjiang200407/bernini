// THIS IS A FILE GENERATED FROM ErrorCode.slang. DO NOT EDIT MANUALLY

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
    // A vertex naming a bone its rig does not have: the palette read would land in a neighbouring
    // instance's slice, which shades as a plausible pose rather than as anything obviously wrong.
    kInvalidBoneIndex,
    // Offset 0 is the reserved null element, so these read zeroes rather than faulting. Defined,
    // but never what the caller meant -- the check is what keeps that from shading silently.
    kNullEntryDeref,
    kNullRangeDeref,
}
;
