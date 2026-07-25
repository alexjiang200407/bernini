public enum VertexSemantic
{
    kPosition,
    kNormal,
    kTangent,
    kColor,
    kTexCoord0,
    kTexCoord1,
    kJoints0,
    kWeights0,
}
;

public enum VertexFormat
{
    kFloat32x2,
    kFloat32x3,
    kFloat32x4,
    kUnorm8x4,
    kUnorm16x2,
    kUnorm16x4,
    kUint16x4,
}
;

public struct VertexAttribute
{
    public VertexSemantic semantic;
    public VertexFormat format;
    // 32-bit, not uint16_t: WGSL has no 16-bit integers. This descriptor is built on the CPU and
    // uploaded (never memcpy'd from a fixed on-disk layout), so widening it costs only buffer size,
    // and the cook's own assetlib::VertexLayout is a separate 16-bit type left untouched.
    public uint byteOffset;
};

public struct VertexLayout
{
    public VertexAttribute attributes[8];
    public uint attributeCount;
    public uint stride;
};
