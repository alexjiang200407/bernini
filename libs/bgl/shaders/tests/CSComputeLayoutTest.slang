import types.ComputeBuffer;

// A mixed uniform layout: a float3 (a 16-byte slot in an MSL cbuffer, not 12), a scalar, then a
// bindless handle at a non-zero, alignment-dependent offset. If the float3 footprint or the handle
// offset were computed wrong, the gpuAddress would be written into the wrong bytes (or seed/tint
// misread), and the readback would not match -- so this pins the Metal cbuffer-layout recompute.
struct Uniforms
{
    float3 tint;
    uint seed;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    gUniforms.outBuffer[idx] = idx * 10u + gUniforms.seed + uint(gUniforms.tint.x);
}
