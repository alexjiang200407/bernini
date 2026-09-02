// WGSL-lowering driver for lib.geom.vertexdecode and lib.geom.Vertex, over a source that is one
// byte-address buffer.
import idl.IVertexSource;
import idl.VertexLayout;
import lib.geom.Vertex;
import lib.geom.vertexdecode;

ByteAddressBuffer gVertices;
RWStructuredBuffer<float4> gOut;

struct StorageSource : IVertexSource
{
    T Load<T>(uint byteOffset) { return gVertices.Load<T>(byteOffset); }
};

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    StorageSource src;
    VertexLayout layout = {};
    let v = DecodeVertex(src, layout, id.x * 64);
    let a = DecodeAttribute(src, id.x * 64, VertexFormat.kUnorm16x4);
    gOut[0] = float4(v.position, 1.0) + v.tangent + a + float4(v.uv, v.joints.x, v.weights.x) + float4(v.normal, 0.0);
}
