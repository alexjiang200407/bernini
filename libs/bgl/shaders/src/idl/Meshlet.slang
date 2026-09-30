public struct Meshlet
{
    public uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    public uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment

    public uint vertexCount;
    public uint triangleCount;

    // xyz is the centre, w the radius.
    public float4 boundingSphere;
};
