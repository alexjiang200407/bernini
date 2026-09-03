import idl.IResolveInputs;
import lib.math.taa;

// The resolve's inputs as this renderer binds them: four bindless textures, the two samplers they
// are read through, and ResolveParams' fields one for one. TaaResolvePass writes every field by
// name.
struct TaaResolveData : IResolveInputs
{
    Texture2D.Handle sceneColor;
    Texture2D.Handle history;
    Texture2D.Handle motionVectors;
    Texture2D.Handle depth;

    float4x4 clipToView;
    float4x4 viewToPrevClip;
    float cameraPairValid;
    float2 jitter;

    // sceneColor, motionVectors and depth are read at their own texel centres and want no filtering;
    // the history is reprojected to an arbitrary sub-pixel position and goes through the resolve's
    // Catmull-Rom taps. The linear sampler also carries the scene colour to the output grid on a
    // frame with no history to reproject.
    SamplerState.Handle pointSampler;
    SamplerState.Handle linearSampler;

    float2 renderSize;
    float2 renderTexelSize;
    float2 outputTexelSize;
    float2 jitterTexels;
    float2 subPixels;
    float resampling;
    float sampleWeightK;
    float blendWeight;
    float historyValid;

    float3 History(float2 uv) { return history.SampleLevel(linearSampler, uv, 0.0).rgb; }
    float4 Scene(float2 uv) { return sceneColor.SampleLevel(pointSampler, uv, 0.0); }
    float3 SceneLinear(float2 uv) { return sceneColor.SampleLevel(linearSampler, uv, 0.0).rgb; }
    float2 Motion(float2 uv) { return motionVectors.SampleLevel(pointSampler, uv, 0.0).xy; }
    float Depth(float2 uv) { return depth.SampleLevel(pointSampler, uv, 0.0).r; }

    ResolveParams Params()
    {
        ResolveParams p;
        p.clipToView = clipToView;
        p.viewToPrevClip = viewToPrevClip;
        p.cameraPairValid = cameraPairValid;
        p.jitter = jitter;
        p.renderSize = renderSize;
        p.renderTexelSize = renderTexelSize;
        p.outputTexelSize = outputTexelSize;
        p.jitterTexels = jitterTexels;
        p.subPixels = subPixels;
        p.resampling = resampling;
        p.sampleWeightK = sampleWeightK;
        p.blendWeight = blendWeight;
        p.historyValid = historyValid;
        return p;
    }
};

ConstantBuffer<TaaResolveData> gTaaResolveData;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        VSOut vout;
        vout.uv = float2((gtid << 1) & 2, gtid & 2);
        vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    TaaResolveData data = gTaaResolveData;
    return TaaResolve(data, data.Params(), input.uv);
}
