import lib.types.ComputeBuffer;

// Writes typed values into a raw UAV at byte offsets, which a CPU readback then checks. The
// readable view has no part in it: what this pins is that a bindless RWByteAddressBuffer resolves
// and stores at all -- on D3D12 through a raw UAV, on Metal through a device pointer.
struct Uniforms
{
    RawComputeBuffer target;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gUniforms.target.Store<float4>(0u, float4(1.0, 2.0, 3.0, 4.0));
    gUniforms.target.Store<uint>(16u, 0xABCDEF01u);
    gUniforms.target.Store<float3>(20u, float3(5.0, 6.0, 7.0));
}
