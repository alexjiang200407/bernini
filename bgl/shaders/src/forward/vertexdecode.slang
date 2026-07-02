import idl.VertexLayout;
import types.ByteBuffer;

public float4 DecodeAttribute(ByteBuffer data, uint attrByteBase, VertexFormat format)
{
    switch (format)
    {
    case VertexFormat.kFloat32x2:
        return float4(asfloat(data.LoadWordAtByte(attrByteBase + 0)), asfloat(data.LoadWordAtByte(attrByteBase + 4)), 0.0, 0.0);
    case VertexFormat.kFloat32x3:
        return float4(
            asfloat(data.LoadWordAtByte(attrByteBase + 0)),
            asfloat(data.LoadWordAtByte(attrByteBase + 4)),
            asfloat(data.LoadWordAtByte(attrByteBase + 8)),
            0.0
        );
    case VertexFormat.kFloat32x4:
        return float4(
            asfloat(data.LoadWordAtByte(attrByteBase + 0)),
            asfloat(data.LoadWordAtByte(attrByteBase + 4)),
            asfloat(data.LoadWordAtByte(attrByteBase + 8)),
            asfloat(data.LoadWordAtByte(attrByteBase + 12))
        );
    case VertexFormat.kUnorm8x4:
    {
        uint w = data.LoadWordAtByte(attrByteBase);
        return float4(w & 0xFFu, (w >> 8) & 0xFFu, (w >> 16) & 0xFFu, (w >> 24) & 0xFFu) / 255.0;
    }
    case VertexFormat.kUnorm16x2:
    {
        uint w = data.LoadWordAtByte(attrByteBase);
        return float4((w & 0xFFFFu) / 65535.0, ((w >> 16) & 0xFFFFu) / 65535.0, 0.0, 0.0);
    }
    case VertexFormat.kUnorm16x4:
    {
        uint w0 = data.LoadWordAtByte(attrByteBase + 0);
        uint w1 = data.LoadWordAtByte(attrByteBase + 4);
        return float4(w0 & 0xFFFFu, (w0 >> 16) & 0xFFFFu, w1 & 0xFFFFu, (w1 >> 16) & 0xFFFFu) / 65535.0;
    }
    case VertexFormat.kUint16x4:
    {
        uint w0 = data.LoadWordAtByte(attrByteBase + 0);
        uint w1 = data.LoadWordAtByte(attrByteBase + 4);
        return float4(w0 & 0xFFFFu, (w0 >> 16) & 0xFFFFu, w1 & 0xFFFFu, (w1 >> 16) & 0xFFFFu);
    }
    default:
        return float4(0.0, 0.0, 0.0, 0.0);
    }
}

public struct DecodedVertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
};

public DecodedVertex DecodeVertex(ByteBuffer data, VertexLayout layout, uint vertexByteBase)
{
    DecodedVertex v;
    v.position = float3(0.0);
    v.normal = float3(0.0);
    v.uv = float2(0.0);

    for (uint i = 0; i < layout.attributeCount; ++i)
    {
        VertexAttribute attr = layout.attributes[i];
        uint attrByteBase = vertexByteBase + attr.byteOffset;
        float4 value = DecodeAttribute(data, attrByteBase, attr.format);

        switch (attr.semantic)
        {
        case VertexSemantic.kPosition:
            v.position = value.xyz;
            break;
        case VertexSemantic.kNormal:
            v.normal = value.xyz;
            break;
        case VertexSemantic.kTexCoord0:
            v.uv = value.xy;
            break;
        default:
            break;
        }
    }

    return v;
}
