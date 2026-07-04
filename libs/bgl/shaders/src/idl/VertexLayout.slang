// THIS IS A FILE GENERATED FROM VertexLayout.slang. DO NOT EDIT MANUALLY

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
    public uint16_t byteOffset;
};

public struct VertexLayout
{
    public VertexAttribute attributes[8];
    public uint16_t attributeCount;
    public uint16_t stride;
};
