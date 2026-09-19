import idl.IResolveInputs;
import lib.math.taa;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<float2> results;
};
ConstantBuffer<Uniforms> gUniforms;

struct Occlusion : IResolveInputs
{
    uint scenario;
    float2 motion;
    float2 ownMotion;
    float2 sampleCentre;
    float renderSize;

    float3 History(float2 uv) { return scenario == 12 ? asfloat(0x7fc00000).xxx : float3(0.8); }
    float HistoryDepth(float2 uv)
    {
        if (scenario == 3 || (scenario == 4 && uv.x > 0.48))
            return 0.0;
        if (scenario == 1 || scenario == 16)
            return 0.6;
        if (scenario == 5 && uv.x > 0.48)
            return 0.7;
        if (scenario == 13)
            return 0.597;
        return 0.3;
    }
    int2 Offset(float2 uv) { return int2(round((uv - sampleCentre) * renderSize)); }
    // Scenarios 19 and 20 put one neighbour, moving far enough to reproject off-screen, nearer and
    // farther than the centre.
    bool Neighbour(float2 uv) { return (scenario == 19 || scenario == 20) && all(Offset(uv) == int2(1, 0)); }

    float4 Scene(float2 uv)
    {
        int2 offset = Offset(uv);
        float value = all(offset == 0) ? 0.2 : (((offset.x + offset.y) & 1) != 0 ? 1.0 : 0.0);
        return float4(value.xxx, scenario == 2 ? 0.5 : 1.0);
    }
    float3 SceneLinear(float2 uv) { return float3(0.2); }
    float4 Motion(float2 uv) { return Neighbour(uv) ? float4(0.6, 0.0, 0.0, 0.0) : float4(motion, ownMotion); }
    float Depth(float2 uv) { return Neighbour(uv) ? (scenario == 19 ? 0.3 : 0.9) : 0.6; }
};

[shader("compute")]
[numthreads(21, 1, 1)]
void main(uint index: SV_DispatchThreadID)
{
    ResolveParams p = {};
    p.clipToView = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    p.viewToPrevView = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    p.cameraPairValid = 1.0;
    p.renderSize = 256.0;
    p.renderTexelSize = 1.0 / 256.0;
    p.outputTexelSize = p.renderTexelSize;
    p.subPixels = 1.0;
    p.blendWeight = 0.05;
    p.historyValid = 1.0;

    Occlusion inputs;
    inputs.scenario = index;
    inputs.motion = float2(0.02, 0.0);
    inputs.ownMotion = float2(0.0, 0.0);
    inputs.sampleCentre = 0.5;
    inputs.renderSize = 256.0;
    float2 uv = 0.5;
    if (index == 6 || index == 12)
        p.historyValid = 0.0;
    if (index == 7)
        p.cameraPairValid = 0.0;
    if (index == 8 || index == 19 || index == 20)
        inputs.motion = 0.0;
    if (index == 9)
    {
        inputs.motion.x = 0.04;
        inputs.ownMotion.x = 0.02;
    }
    if (index == 10)
        p.clipToView[2][2] = 120000.0;
    if (index == 11)
        p.viewToPrevView[2][3] = -0.3;
    if (index == 14)
        inputs.motion.x = 0.6;
    if (index == 15 || index == 16)
    {
        p.resampling = 1.0;
        p.subPixels = 4.0;
        p.outputTexelSize /= 4.0;
        p.jitterTexels = 0.375;
        p.sampleWeightK = 8.0;
        uv = 128.125 / 256.0;
        inputs.sampleCentre = 128.5 / 256.0;
    }
    if (index == 16)
    {
        p.subPixels = 8.0;
        p.outputTexelSize = p.renderTexelSize / 8.0;
        p.jitterTexels = 0.4375;
        uv = 128.0625 / 256.0;
    }
    if (index == 17 || index == 18)
    {
        p.renderSize = 768.0;
        p.renderTexelSize = 1.0 / 768.0;
        p.outputTexelSize = p.renderTexelSize;
        inputs.renderSize = 768.0;
        inputs.motion.x = 0.4 + (index == 18 ? 0.004 : 0.0);
        inputs.ownMotion.x = index == 18 ? 0.004 : 0.0;
    }
    float4 result = TaaResolve(inputs, p, uv);
    gUniforms.results[index] = result.ra;
}
