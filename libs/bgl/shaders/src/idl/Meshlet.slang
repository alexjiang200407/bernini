// THIS IS A FILE GENERATED FROM Meshlet.slang. DO NOT EDIT MANUALLY

public struct Meshlet
{
    public uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    public uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment

    public uint vertexCount;
    public uint triangleCount;

    // xyz is the centre, w the radius. One float4 rather than a float3 and a float because MSL
    // sizes a float3 at 16 bytes where the C/C++ mirror gives it 12, which puts every following
    // member at a different offset on Metal -- a mismatch no trailing padding can close.
    public float4 boundingSphere;
};
