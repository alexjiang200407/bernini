// THIS IS A FILE GENERATED FROM Meshlet.slang. DO NOT EDIT MANUALLY

public struct Meshlet
{
    public uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    public uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment

    // Both are bounded by cMaxVerticesPerMeshlet / cMaxPrimsPerMeshlet, so a uint16_t would hold
    // them -- but core WGSL has no 16-bit scalar, and Scene builds this struct field by field rather
    // than reading it from a cooked file, so nothing constrains the width.
    public uint vertexCount;
    public uint triangleCount;

    public float3 boundingCenter;
    public float boundingRadius;
};
