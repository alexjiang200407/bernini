import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;
import lib.types.ComputeBuffer;

// A surface declared the way a game declares one, compiled without the arena: a slot field, a
// parameter struct with defaults of one, two and four components, and the associated type. What it
// pins is the contract's grammar on this compiler, and whether PbrSurface's field initialisers
// apply to a surface that sets only its base colour.
struct ProbeParams
{
    [Default(1.0)]
    float scalar;

    [Default(1.0, 2.0)]
    float2 pair;

    [Default(1.0, 2.0, 3.0, 4.0)]
    float4 quad;

    ColorSlot base;
};

struct ProbeSurface : ISurfaceSource
{
    typealias MaterialParams = ProbeParams;

    static float Coverage<R : IMaterialReader>(R reader, ProbeParams params) { return 1.0; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, ProbeParams params)
    {
        // Constructed, not declared: a bare `PbrSurface surface;` applies no field default.
        PbrSurface surface = PbrSurface();
        surface.baseColor = reader.Sample(params.base, reader.Uv());
        return surface;
    }
};

// The reader with nothing behind it: a slot samples as a constant, so the value that comes back
// says the slot's index reached the sample and nothing else.
struct ConstantReader : IMaterialReader
{
    float4 Sample<S : ISlot>(S slot, float2 uv) { return float4(0.25 * float(slot.Index() + 1u), 0.0, 0.0, 1.0); }
    float2 Uv() { return float2(0.0, 0.0); }
    float3 WorldPos() { return float3(0.0, 0.0, 0.0); }
    float3 WorldNormal() { return float3(0.0, 1.0, 0.0); }
    float4 WorldTangent() { return float4(1.0, 0.0, 0.0, 1.0); }
    float3 CameraPos() { return float3(0.0, 0.0, 0.0); }
};

struct Uniforms
{
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    ProbeParams params;
    params.base.index = 1u;

    ConstantReader reader;
    let surface = ProbeSurface.Evaluate(reader, params);

    // Hundreds: the roughness default, 1.0, that Evaluate never set. Units: the sampled slot,
    // 0.5 from index 1, times four.
    gUniforms.outBuffer[0] = uint(surface.orm.y * 100.0) + uint(surface.baseColor.r * 4.0);
}
