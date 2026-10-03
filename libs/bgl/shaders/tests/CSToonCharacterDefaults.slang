import bgl.ToonCharacterSurface;
import lib.types.ComputeBuffer;

// A default-constructed ToonCharacterSurface, field by field, for a CPU comparison against what the
// contract says a surface that sets nothing means.
struct Uniforms
{
    ComputeBuffer<float4> outValues;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    let surface = ToonCharacterSurface();
    gUniforms.outValues[0] = surface.baseColor;
    gUniforms.outValues[1] = float4(surface.firstShade, surface.baseStep);
    gUniforms.outValues[2] = float4(surface.secondShade, surface.shadeStep);
    gUniforms.outValues[3] = float4(surface.baseFeather, surface.shadeFeather, surface.shadeOffset, surface.face);
}
