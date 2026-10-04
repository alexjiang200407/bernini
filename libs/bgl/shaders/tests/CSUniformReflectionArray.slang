import lib.types.ComputeBuffer;

// An array between two members: its elements are 16 bytes apart, and the member after it starts where
// the last one ends.
struct Uniforms
{
    float4 before;
    float4 planes[6];
    float3 after;
    float tail;
    ComputeBuffer<float4> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    gUniforms.outBuffer[idx] = gUniforms.before + gUniforms.planes[idx % 6] + float4(gUniforms.after, gUniforms.tail);
}
