import types.ComputeBuffer;

// Writes a known pattern into a bindless RWStructuredBuffer. A CPU readback then verifies the exact
// values, which proves end to end that Slang->target codegen and the bindless handle resolution
// (on Metal, the slot-index -> gpuAddress translation at dispatch) are correct.
struct Uniforms
{
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    gUniforms.outBuffer[idx] = idx * 10u + 1u;
}
