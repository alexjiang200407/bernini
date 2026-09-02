// WGSL-lowering driver for lib.math.taa. Compute: every read the resolve makes is at mip zero. The
// inputs here are plain 2D textures -- the first IResolveInputs with no bindless handle.
import idl.IResolveInputs;
import lib.math.taa;

Texture2D gHistory;
Texture2D gScene;
Texture2D gMotion;
Texture2D gDepth;
SamplerState gPoint;
SamplerState gLinear;
RWStructuredBuffer<float4> gOut;

struct BoundInputs : IResolveInputs
{
    float3 History(float2 uv) { return gHistory.SampleLevel(gLinear, uv, 0.0).rgb; }
    float4 Scene(float2 uv) { return gScene.SampleLevel(gPoint, uv, 0.0); }
    float3 SceneLinear(float2 uv) { return gScene.SampleLevel(gLinear, uv, 0.0).rgb; }
    float2 Motion(float2 uv) { return gMotion.SampleLevel(gPoint, uv, 0.0).xy; }
    float Depth(float2 uv) { return gDepth.SampleLevel(gPoint, uv, 0.0).r; }
};

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let c = float3(id) * 0.1;
    let y = RgbToYCoCg(ToneWeight(c));
    let back = UnToneWeight(YCoCgToRgb(y));
    let w = PhaseWeight(0.25, 0.125, 2.0, 1.0);
    let m = CameraMotion(float2(0.5, 0.5), 0.5, float2(0.0, 0.0), float4x4(1.0), float4x4(1.0));

    BoundInputs inputs;
    ResolveParams p;
    p.clipToView = float4x4(1.0);
    p.viewToPrevClip = float4x4(1.0);
    p.cameraPairValid = 1.0;
    p.jitter = float2(0.0, 0.0);
    p.renderSize = float2(64.0, 64.0);
    p.renderTexelSize = float2(1.0 / 64.0, 1.0 / 64.0);
    p.outputTexelSize = float2(1.0 / 128.0, 1.0 / 128.0);
    p.jitterTexels = float2(0.125, -0.125);
    p.subPixels = float2(2.0, 2.0);
    p.resampling = 1.0;
    p.sampleWeightK = 1.0;
    p.blendWeight = 0.1;
    p.historyValid = 1.0;
    let resolved = Resolve(inputs, p, float2(0.5, 0.5));

    gOut[0] = float4(back, w) + float4(m, 0.0, 0.0) + resolved;
}
