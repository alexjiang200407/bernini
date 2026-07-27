// THIS IS A FILE GENERATED FROM Meshlet.slang. DO NOT EDIT MANUALLY

public struct Meshlet
{
    public uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    public uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment

    public uint vertexCount;
    public uint triangleCount;

    public float3 boundingCenter;
    public float boundingRadius;
};
