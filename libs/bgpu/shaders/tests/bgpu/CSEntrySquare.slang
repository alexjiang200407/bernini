import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;

// Reads each live element of an EntryBuffer and writes the square, so a readback
// proves the buffer family's Slang half against its C++ half with no renderer in the process.
struct Uniforms
{
    EntryBuffer<uint> values;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint idx: SV_DispatchThreadID)
{
    // Element 0 is the reserved null element, so the live ones start at 1.
    let value = gUniforms.values[idx + 1u];
    gUniforms.outBuffer[idx] = value * value;
}
