
struct Meshlet {
    uint relativeVertexOffset; // Relative to the parent Mesh vertexSegment
    uint relativeIndexOffset;  // Relative to the parent Mesh indexSegment
    
    uint16_t vertexCount;
    uint16_t triangleCount;

    float3 boundingCenter;
    float  boundingRadius;
};
