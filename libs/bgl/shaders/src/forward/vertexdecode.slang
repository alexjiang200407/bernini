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

#if defined(BERNINI_GPU_DEBUG)
    bool4 found = { false, false, false, false };
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
            found[0] = true;
#endif
            break;
        case VertexSemantic.kNormal:
            v.normal = value.xyz;
#if defined(BERNINI_GPU_DEBUG)
            found[1] = true;
#endif
            break;
        case VertexSemantic.kTexCoord0:
            v.uv = value.xy;
#if defined(BERNINI_GPU_DEBUG)
            found[2] = true;
#endif
            break;
        case VertexSemantic.kTangent:
            v.tangent = value;
#if defined(BERNINI_GPU_DEBUG)
            found[3] = true;
#endif
            break;
        default:
            break;
        }
    }

#if defined(BERNINI_GPU_DEBUG)
    for (uint j = 0; j < 4; ++j)
    {
        dbg_assert(found[j], ErrorCode.kInvalidVertexLayout);
    }
#endif

    return v;
}
