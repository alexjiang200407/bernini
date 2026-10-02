// Writes 100 + i into elements [first, first + count) of an EntryBuffer through its writable view,
// so a readback proves the view addresses the buffer's own bytes.
struct Uniforms
{
    RWStructuredBuffer<uint, ScalarDataLayout>.Handle values;
    uint first;
    uint count;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    if (idx < gUniforms.count)
    {
        gUniforms.values[gUniforms.first + idx] = 100u + idx;
    }
}
