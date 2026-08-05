import idl.TextureHandle;

struct TaaResolveData
{
    TextureHandle sceneColor;
    TextureHandle history;
    TextureHandle motionVectors;

    // sceneColor and motionVectors are read 1:1 and want no filtering; the history is reprojected to
    // an arbitrary sub-pixel position and wants bilinear.
    SamplerState.Handle pointSampler;
    SamplerState.Handle linearSampler;

    float2 texelSize;

    // How much of the resolved pixel comes from this frame. Zero history -- the first frame, or the
    // first after a resize -- takes the scene colour whole.
    float blendWeight;
    float historyValid;
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

// Chrominance-decorrelated luma space. The neighbourhood bounds are an axis-aligned box, and in RGB
// that box is a poor fit to the set of colours an edge actually produces -- it admits history the
// pixel could not have come from, which is what ghosting is.
float3 RgbToYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 YCoCgToRgb(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Weighting the blend by inverse luma stops one bright sample from dominating the average, and
// undoing it afterwards keeps the result linear. The clamp keeps the inverse finite for a luma that
// reaches 1 after the forward step.
float3 ToneWeight(float3 c)
{
    return c / (1.0 + max(max(c.r, c.g), c.b));
}

float3 UnToneWeight(float3 c)
{
    return c / max(1e-4, 1.0 - max(max(c.r, c.g), c.b));
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let pointClamp = gTaaResolveData.pointSampler;
    let texel = gTaaResolveData.texelSize;

    float4 current = gTaaResolveData.sceneColor.SampleLevel(pointClamp, input.uv, 0.0);

    // The 3x3 the history is admitted from. Bounds are taken in YCoCg, and the centre sample is the
    // one the blend starts from.
    float3 centre = RgbToYCoCg(current.rgb);
    float3 boxMin = centre;
    float3 boxMax = centre;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            float2 tapUv = input.uv + float2(x, y) * texel;
            float3 tap = RgbToYCoCg(gTaaResolveData.sceneColor.SampleLevel(pointClamp, tapUv, 0.0).rgb);

            boxMin = min(boxMin, tap);
            boxMax = max(boxMax, tap);
        }
    }

    float2 motion = gTaaResolveData.motionVectors.SampleLevel(pointClamp, input.uv, 0.0).xy;
    float2 historyUv = input.uv - motion;

    // Reprojected off-screen: there is no history for this pixel, and wrapping the clamp sampler's
    // edge texel in would be a smear rather than an accumulation.
    bool onScreen = all(historyUv >= 0.0) && all(historyUv <= 1.0);

    // Branched, not folded into the blend weight below: the history texture is not cleared, so on the
    // first frame it holds whatever the allocation did, which may decode as NaN. `lerp(NaN, c, 1.0)`
    // is NaN -- the zero coefficient does not discard the operand -- and a NaN written into the
    // accumulation stays there for the life of the target.
    if (!onScreen || gTaaResolveData.historyValid == 0.0)
    {
        return current;
    }

    float3 history = gTaaResolveData.history.SampleLevel(gTaaResolveData.linearSampler, historyUv, 0.0).rgb;

    // Clamped rather than rejected: a pixel whose history is merely stale still carries subpixel
    // detail worth keeping once it is pulled inside what this frame could have produced.
    history = YCoCgToRgb(clamp(RgbToYCoCg(history), boxMin, boxMax));

    float3 resolved = UnToneWeight(lerp(ToneWeight(history), ToneWeight(current.rgb), gTaaResolveData.blendWeight));

    return float4(resolved, current.a);
}
