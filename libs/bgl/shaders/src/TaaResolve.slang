import idl.TextureHandle;

struct TaaResolveData
{
    TextureHandle sceneColor;
    TextureHandle history;
    TextureHandle motionVectors;

    // sceneColor and motionVectors are read 1:1 and want no filtering; the history is reprojected
    // to an arbitrary sub-pixel position and goes through SampleHistoryCatmullRom's bilinear taps.
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

// Catmull-Rom over the 4x4 around `uv`, folded to five bilinear fetches by pairing the two inner
// taps per axis and dropping the four corners, whose weights are near zero (renormalised out).
//
// A plain bilinear fetch convolves the history with a tent every frame it lands off a texel
// centre, which under sustained motion is every frame -- the accumulation re-blurs each hop and
// the image softens until the camera stops. The negative lobes here undo that. They can also
// overshoot, so the result is only safe once the neighbourhood clamp has pulled it inside the box.
float3 SampleHistoryCatmullRom(float2 uv, float2 texel)
{
    let history = gTaaResolveData.history;
    let linearClamp = gTaaResolveData.linearSampler;

    float2 samplePos = uv / texel;
    float2 innerTexel = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - innerTexel;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    // The two inner taps collapse to one bilinear fetch placed where their weights balance.
    float2 w12 = w1 + w2;
    float2 uv0 = (innerTexel - 1.0) * texel;
    float2 uv3 = (innerTexel + 2.0) * texel;
    float2 uv12 = (innerTexel + w2 / w12) * texel;

    float3 sum = history.SampleLevel(linearClamp, float2(uv12.x, uv0.y), 0.0).rgb * (w12.x * w0.y) +
                 history.SampleLevel(linearClamp, float2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y) +
                 history.SampleLevel(linearClamp, uv12, 0.0).rgb * (w12.x * w12.y) +
                 history.SampleLevel(linearClamp, float2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y) +
                 history.SampleLevel(linearClamp, float2(uv12.x, uv3.y), 0.0).rgb * (w12.x * w3.y);

    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;

    return sum / weight;
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let pointClamp = gTaaResolveData.pointSampler;
    let texel = gTaaResolveData.texelSize;

    float4 current = gTaaResolveData.sceneColor.SampleLevel(pointClamp, input.uv, 0.0);

    float2 motion = gTaaResolveData.motionVectors.SampleLevel(pointClamp, input.uv, 0.0).xy;
    float2 historyUv = input.uv - motion;

    // The 3x3 the history is admitted from: min/max bounds at rest, tightening to mean +/- sigma
    // as the pixel moves. On stochastic coverage the min/max box spans both populations whole and
    // admits every dragged mixture -- the smear a hashed surface trails at a distance -- where the
    // sigma box recentres on the majority. Motion is jitter-free, so rest is exactly min/max.
    // The trade-off history is in docs/taa.md.
    float3 centre = RgbToYCoCg(current.rgb);
    float3 boxMin = centre;
    float3 boxMax = centre;
    float3 m1 = centre;
    float3 m2 = centre * centre;

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
            m1 += tap;
            m2 += tap * tap;
        }
    }

    // Fully tight from one texel of motion per frame. The sigma box is kept inside the min/max
    // box: nine bounded samples can put mu +/- sigma outside it, and outside it the clamp would
    // admit colours the neighbourhood never produced.
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mu * mu, 0.0));
    float3 tightMin = clamp(mu - sigma, boxMin, boxMax);
    float3 tightMax = clamp(mu + sigma, boxMin, boxMax);
    float tightness = saturate(length(motion / texel));
    boxMin = lerp(boxMin, tightMin, tightness);
    boxMax = lerp(boxMax, tightMax, tightness);

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

    float3 history = SampleHistoryCatmullRom(historyUv, texel);

    // Clamped rather than rejected: a pixel whose history is merely stale still carries subpixel
    // detail worth keeping once it is pulled inside what this frame could have produced.
    history = YCoCgToRgb(clamp(RgbToYCoCg(history), boxMin, boxMax));

    float3 resolved = UnToneWeight(lerp(ToneWeight(history), ToneWeight(current.rgb), gTaaResolveData.blendWeight));

    return float4(resolved, current.a);
}
