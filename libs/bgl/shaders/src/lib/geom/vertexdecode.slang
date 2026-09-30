import idl.IVertexSource;
import idl.VertexLayout;
import lib.geom.Vertex;

// What a layout with no second UV set decodes to. Outside the [0, 1] square a lightmap unwrap lives
// in, and constant across every triangle it reaches, so it interpolates to itself.
public static const float2 cNoUv1 = float2(-1.0, -1.0);

// Whether `uv1` came from a real second UV set rather than cNoUv1.
public bool HasUv1(float2 uv1)
{
    return uv1.x > -0.5;
}

public struct DecodedVertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
    public float2 uv1;
    public float4 tangent;

    // Skin binding, and only the skinned tier reads it. `joints` are rig-local bone indices;
    // `weights` default to (1,0,0,0) so an unskinned layout decodes as fully bound to bone 0 rather
    // than to nothing -- a zero weight vector would collapse the vertex onto the origin.
    public float4 joints;
    public float4 weights;
};

public DecodedVertex DecodeVertex<S : IVertexSource>(S data, VertexLayout layout, uint vertexByteBase)
{
    DecodedVertex v;
    v.position = float3(0.0, 0.0, 0.0);
    v.normal = float3(0.0, 0.0, 1.0);
    v.uv = float2(0.0, 0.0);
    v.uv1 = cNoUv1;
    v.tangent = float4(0.0, 0.0, 0.0, 0.0);
    v.joints = float4(0.0, 0.0, 0.0, 0.0);
    v.weights = float4(1.0, 0.0, 0.0, 0.0);

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
        case VertexSemantic.kTexCoord1:
            v.uv1 = value.xy;
            break;
        case VertexSemantic.kTangent:
            v.tangent = value;
            break;
        case VertexSemantic.kJoints0:
            v.joints = value;
            break;
        case VertexSemantic.kWeights0:
            v.weights = value;
            break;
        default:
            break;
        }
    }

    return v;
}
