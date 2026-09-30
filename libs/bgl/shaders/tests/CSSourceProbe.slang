import game.probe;
import lib.types.ComputeBuffer;

// Writes whatever game.probe says, which is the file under game/ unless a module of that name was
// loaded from source first.
struct Uniforms
{
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gUniforms.outBuffer[0] = kProbeValue;
}
