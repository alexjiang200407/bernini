import idl.IVertexSource;
import idl.VertexLayout;

// @pre attrByteBase is 4-byte aligned, which every producer's layout keeps and Scene checks on
// the way in. The float formats load as vectors; the packed ones are still assembled by hand,
// because a raw buffer does no format conversion -- that is what a typed view is for.
public float4 DecodeAttribute<S : IVertexSource>(S data, uint attrByteBase, VertexFormat format)
{
    switch (format)
    {
    case VertexFormat.kFloat32x2:
        return float4(data.Load<float2>(attrByteBase), 0.0, 0.0);
    case VertexFormat.kFloat32x3:
        return float4(data.Load<float3>(attrByteBase), 0.0);
    case VertexFormat.kFloat32x4:
        return data.Load<float4>(attrByteBase);
    case VertexFormat.kUnorm8x4:
    {
        uint w = data.Load<uint>(attrByteBase);
        return float4(w & 0xFFu, (w >> 8) & 0xFFu, (w >> 16) & 0xFFu, (w >> 24) & 0xFFu) / 255.0;
    }
    case VertexFormat.kUnorm16x2:
    {
        uint w = data.Load<uint>(attrByteBase);
        return float4((w & 0xFFFFu) / 65535.0, ((w >> 16) & 0xFFFFu) / 65535.0, 0.0, 0.0);
    }
    case VertexFormat.kUnorm16x4:
    {
        uint w0 = data.Load<uint>(attrByteBase + 0);
        uint w1 = data.Load<uint>(attrByteBase + 4);
        return float4(w0 & 0xFFFFu, (w0 >> 16) & 0xFFFFu, w1 & 0xFFFFu, (w1 >> 16) & 0xFFFFu) / 65535.0;
    }
    case VertexFormat.kUint16x4:
    {
        uint w0 = data.Load<uint>(attrByteBase + 0);
        uint w1 = data.Load<uint>(attrByteBase + 4);
        return float4(w0 & 0xFFFFu, (w0 >> 16) & 0xFFFFu, w1 & 0xFFFFu, (w1 >> 16) & 0xFFFFu);
    }
    default:
        return float4(0.0, 0.0, 0.0, 0.0);
    }
}
