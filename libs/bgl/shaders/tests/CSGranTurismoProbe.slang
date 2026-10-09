import lib.types.ComputeBuffer;
import lib.math.Tonemap;

struct Uniforms
{
    ComputeBuffer<float4> outColor;
    float3 sceneLinear;
};

ConstantBuffer<Uniforms> gUniforms;

// Gran Turismo's curve over one colour. Calls GranTurismo rather than restating it, so the suite
// measures what PostProcess runs.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    gUniforms.outColor[0] = float4(GranTurismo(gUniforms.sceneLinear), 1.0);
}
