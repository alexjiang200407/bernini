public struct Meshlet
{
    public uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    public uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment

    public uint16_t vertexCount;
    public uint16_t triangleCount;

    public float3 boundingCenter;
    public float boundingRadius;
};
