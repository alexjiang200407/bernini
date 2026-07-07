import idl.VertexLayout;
import types.ByteBuffer;
import debug.dbg;
import idl.ErrorCode;
import util.Vertex;

public struct DecodedVertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
    public float4 tangent;
};

public DecodedVertex DecodeVertex(ByteBuffer data, VertexLayout layout, uint vertexByteBase)
{
    DecodedVertex v;
    v.position = float3(0.0, 0.0, 0.0);
    v.normal = float3(0.0, 0.0, 1.0);
    v.uv = float2(0.0, 0.0);
    v.tangent = float4(0.0, 0.0, 0.0, 0.0);

#if defined(BERNINI_GPU_DEBUG)
    bool hasPosition = false;
#endif

    for (uint i = 0; i < layout.attributeCount; ++i)
    {
        VertexAttribute attr = layout.attributes[i];
        uint attrByteBase = vertexByteBase + attr.byteOffset;
        float4 value = DecodeAttribute(data, attrByteBase, attr.format);

        switch (attr.semantic)
        {
        case VertexSemantic.kPosition:
            v.position = value.xyz;
#if defined(BERNINI_GPU_DEBUG)
            hasPosition = true;
#endif
            break;
        case VertexSemantic.kNormal:
            v.normal = value.xyz;
            break;
        case VertexSemantic.kTexCoord0:
            v.uv = value.xy;
            break;
        case VertexSemantic.kTangent:
            v.tangent = value;
            break;
        default:
            break;
        }
    }

#if defined(BERNINI_GPU_DEBUG)
    // Position is the only attribute a mesh cannot render without; the rest use the defaults above.
    dbg_assert(hasPosition, ErrorCode.kInvalidVertexLayout);
#endif

    return v;
}
