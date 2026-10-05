import lib.math.ToonShading;
import lib.types.ComputeBuffer;
import lib.types.RawBuffer;

// The normal a face's base tone shades with, per case, for a CPU comparison against the same
// formula. A case is 112 bytes of float4s: the normal, the head-space point, the three rows of
// headFromWorld, the block's faceEllipsoid, and the weight in x.
struct Uniforms
{
    RawBuffer records;
    ComputeBuffer<float4> outValues;
    uint caseCount;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    for (uint i = 0; i < gUniforms.caseCount; ++i)
    {
        let base = i * 112u;
        let normal = gUniforms.records.Load<float4>(base).xyz;
        let headPos = gUniforms.records.Load<float4>(base + 16u).xyz;
        let headFromWorld = float4x4(
            gUniforms.records.Load<float4>(base + 32u),
            gUniforms.records.Load<float4>(base + 48u),
            gUniforms.records.Load<float4>(base + 64u),
            float4(0.0, 0.0, 0.0, 1.0)
        );
        let faceEllipsoid = gUniforms.records.Load<float4>(base + 80u);
        let weight = gUniforms.records.Load<float4>(base + 96u).x;
        gUniforms.outValues[i] = float4(ToonFaceNormal(normal, headPos, headFromWorld, faceEllipsoid, weight), 0.0);
    }
}
